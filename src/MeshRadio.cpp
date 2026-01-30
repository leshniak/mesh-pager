#include "MeshRadio.h"

#include <RadioLib.h>
#include <mbedtls/aes.h>
#include <mbedtls/base64.h>

// ==== HW binding (board provides these macros) ====
static SX1262 radio = new Module(LORA_CS, LORA_IRQ, RADIOLIB_NC, LORA_BUSY);

// ==== Mesh constants ====
static constexpr uint32_t meshToBroadcast = 0xFFFFFFFF;
static constexpr uint8_t meshPortTextMessageApp = 1;

// ==== Limits / buffers ====
static constexpr size_t meshHeaderLen = 16;
static constexpr size_t rxBufferLen = 255;
static constexpr size_t maxDecryptLen = 240;
static constexpr size_t maxTextLen = 127;

// ==== RX IRQ flag ====
static volatile bool rxPacketReady = false;
static void onDio1Rx() {
  rxPacketReady = true;
}

bool MeshRadio::begin(const Config& cfg) {
  config = cfg;

  if (!config.channelName || !config.channelKeyBase64) {
    lastError = -1001;
    return false;
  }

  if (!decodeBase64Key32(config.channelKeyBase64, channelKey)) {
    lastError = -1002;
    return false;
  }

  nodeId = getMeshtasticNodeIdFromMac(config.macAddress);
  randomSeed((uint32_t)esp_random());
  packetIdCounter = (uint32_t)random(0x1000, 0xFFFF);

  channelHash = computeChannelHashByte(config.channelName, channelKey);

  int beginResult = radio.begin();
  if (beginResult != RADIOLIB_ERR_NONE) {
    lastError = beginResult;
    return false;
  }

  // Radio config
  radio.setDio2AsRfSwitch(true);
  radio.setFrequency(config.frequencyMHz);
  radio.setBandwidth(config.bandwidthKHz);
  radio.setSpreadingFactor(config.spreadingFactor);
  radio.setCodingRate(config.codingRate);
  radio.setSyncWord(config.syncWord);
  radio.setPreambleLength(config.preambleLength);
  radio.setTCXO(config.tcxoVoltage);

  // Max TX power
  radio.setOutputPower(config.outputPowerDbm);
  radio.setCurrentLimit(config.currentLimitmA);

  // RX IRQ
  radio.setDio1Action(onDio1Rx);
  radio.startReceive();

  lastError = 0;
  return true;
}

bool MeshRadio::sleep() {
  const bool state = radio.sleep();

  if (state != RADIOLIB_ERR_NONE) {
    return false;
  }

  return true;
}

void MeshRadio::pollRx() {
  if (!rxPacketReady) {
    return;
  }
  rxPacketReady = false;

  const int packetLen = radio.getPacketLength();
  if (packetLen <= 0 || packetLen > (int)rxBufferLen) {
    radio.startReceive();
    return;
  }

  uint8_t rxBuffer[rxBufferLen];
  const int readResult = radio.readData(rxBuffer, packetLen);
  if (readResult != RADIOLIB_ERR_NONE) {
    lastError = readResult;
    radio.startReceive();
    return;
  }

  if (packetLen < (int)meshHeaderLen + 2) {
    radio.startReceive();
    return;
  }

  const uint32_t toNode = getLe32(rxBuffer + 0);
  const uint32_t fromNode = getLe32(rxBuffer + 4);
  const uint32_t packetId = getLe32(rxBuffer + 8);
  const uint8_t rxChannelHash = rxBuffer[13];

  (void)toNode;  // not used currently

  if (rxChannelHash != channelHash) {
    radio.startReceive();
    return;
  }
  if (fromNode == nodeId) {
    radio.startReceive();
    return;
  }

  const size_t encryptedLen = (size_t)packetLen - meshHeaderLen;
  if (encryptedLen == 0 || encryptedLen > maxDecryptLen) {
    radio.startReceive();
    return;
  }

  uint8_t decrypted[maxDecryptLen];
  memcpy(decrypted, rxBuffer + meshHeaderLen, encryptedLen);

  // decrypt: nonce(packetId, fromNode)
  aesCtrCryptMeshtastic(decrypted, encryptedLen, packetId, fromNode, channelKey);

  uint32_t portnum = 0;
  const uint8_t* payload = nullptr;
  size_t payloadLen = 0;

  if (!parseDataText(decrypted, encryptedLen, portnum, payload, payloadLen)) {
    radio.startReceive();
    return;
  }

  if (portnum == meshPortTextMessageApp && payload && payloadLen && textRxCallback) {
    static char textOut[200];

    const size_t maxCopy = sizeof(textOut) - 1;
    const size_t copyLen = (payloadLen < maxCopy) ? payloadLen : maxCopy;

    for (size_t index = 0; index < copyLen; index++) {
      const uint8_t ch = payload[index];
      textOut[index] = (ch >= 32 && ch <= 126) ? (char)ch : '.';
    }
    textOut[copyLen] = '\0';

    textRxCallback(fromNode, textOut);
  }

  radio.startReceive();
}

bool MeshRadio::txText(const char* text) {
  if (!text) {
    return false;
  }

  const size_t textLen = strlen(text);
  if (textLen == 0 || textLen > maxTextLen) {
    lastError = -1101;
    return false;
  }

  const uint32_t packetId = packetIdCounter++;
  const uint8_t flags = makeMeshFlags(config.hopLimit, config.wantAck, config.viaMqtt, config.hopStart);

  uint8_t protobufPayload[2 + 2 + 1 + maxTextLen];
  const size_t protobufLen = buildDataProtobufText(protobufPayload, sizeof(protobufPayload),
                                                   (const uint8_t*)text, textLen);
  if (!protobufLen) {
    lastError = -1102;
    return false;
  }

  aesCtrCryptMeshtastic(protobufPayload, protobufLen, packetId, nodeId, channelKey);

  uint8_t meshFrame[meshHeaderLen + sizeof(protobufPayload)];
  const size_t frameLen = buildMeshPacket(meshFrame, sizeof(meshFrame),
                                          meshToBroadcast, nodeId, packetId,
                                          flags, channelHash,
                                          protobufPayload, protobufLen);
  if (!frameLen) {
    lastError = -1103;
    return false;
  }

  const int transmitResult = radio.transmit(meshFrame, frameLen);
  if (transmitResult != RADIOLIB_ERR_NONE) {
    lastError = transmitResult;
    return false;
  }

  radio.startReceive();
  lastError = 0;
  return true;
}

// ---------------- Internal helpers ----------------

bool MeshRadio::decodeBase64Key32(const char* base64Key, uint8_t decodedKey[32]) {
  size_t decodedLen = 0;
  int decodeResult = mbedtls_base64_decode(decodedKey, 32, &decodedLen,
                                           (const unsigned char*)base64Key,
                                           strlen(base64Key));
  return (decodeResult == 0 && decodedLen == 32);
}

uint32_t MeshRadio::getMeshtasticNodeIdFromMac(const uint8_t macBytes[6]) {
  return ((uint32_t)macBytes[2] << 24) | ((uint32_t)macBytes[3] << 16) | ((uint32_t)macBytes[4] << 8) | ((uint32_t)macBytes[5] << 0);
}

uint8_t MeshRadio::xorHashBytes(const uint8_t* bytes, size_t length) {
  uint8_t hash = 0;
  for (size_t index = 0; index < length; index++) {
    hash ^= bytes[index];
  }
  return hash;
}

uint8_t MeshRadio::computeChannelHashByte(const char* channelName, const uint8_t keyBytes[32]) {
  const uint8_t nameHash = xorHashBytes((const uint8_t*)channelName, strlen(channelName));
  const uint8_t keyHash = xorHashBytes(keyBytes, 32);
  return (uint8_t)(nameHash ^ keyHash);
}

uint8_t MeshRadio::makeMeshFlags(uint8_t hopLimit, bool wantAck, bool viaMqtt, uint8_t hopStart) {
  return (uint8_t)((hopLimit & 0x07) | (wantAck ? 0x08 : 0x00) | (viaMqtt ? 0x10 : 0x00) | ((hopStart & 0x07) << 5));
}

void MeshRadio::aesCtrCryptMeshtastic(uint8_t* buffer, size_t bufferLen,
                                      uint32_t packetId, uint32_t fromNode,
                                      const uint8_t keyBytes[32]) {
  uint8_t nonce[16] = { 0 };
  uint8_t streamBlock[16] = { 0 };
  size_t ncOff = 0;

  const uint64_t packetIdU64 = (uint64_t)packetId;
  memcpy(nonce + 0, &packetIdU64, 8);
  memcpy(nonce + 8, &fromNode, 4);

  mbedtls_aes_context aesCtx;
  mbedtls_aes_init(&aesCtx);
  mbedtls_aes_setkey_enc(&aesCtx, keyBytes, 256);
  mbedtls_aes_crypt_ctr(&aesCtx, bufferLen, &ncOff, nonce, streamBlock, buffer, buffer);
  mbedtls_aes_free(&aesCtx);
}

inline void MeshRadio::putLe32(uint8_t* dst, uint32_t value) {
  dst[0] = (uint8_t)(value >> 0);
  dst[1] = (uint8_t)(value >> 8);
  dst[2] = (uint8_t)(value >> 16);
  dst[3] = (uint8_t)(value >> 24);
}

inline uint32_t MeshRadio::getLe32(const uint8_t* src) {
  return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

size_t MeshRadio::buildMeshPacket(uint8_t* outFrame, size_t outFrameMax,
                                  uint32_t toNode, uint32_t fromNode, uint32_t packetId,
                                  uint8_t flags, uint8_t channelHashByte,
                                  const uint8_t* encryptedPayload, size_t encryptedPayloadLen) {
  if (outFrameMax < meshHeaderLen + encryptedPayloadLen) {
    return 0;
  }

  putLe32(outFrame + 0, toNode);
  putLe32(outFrame + 4, fromNode);
  putLe32(outFrame + 8, packetId);

  outFrame[12] = flags;
  outFrame[13] = channelHashByte;
  outFrame[14] = 0x00;  // nextHop
  outFrame[15] = 0x00;  // relayNode

  memcpy(outFrame + meshHeaderLen, encryptedPayload, encryptedPayloadLen);
  return meshHeaderLen + encryptedPayloadLen;
}

size_t MeshRadio::buildDataProtobufText(uint8_t* outBuffer, size_t outBufferMax,
                                        const uint8_t* textBytes, size_t textLen) {
  if (textLen == 0 || textLen > maxTextLen) {
    return 0;
  }

  const size_t requiredLen = 2 + 2 + 1 + textLen;  // (0x08 port) + (0x12 len) + payload
  if (outBufferMax < requiredLen) {
    return 0;
  }

  size_t writeOffset = 0;
  outBuffer[writeOffset++] = 0x08;
  outBuffer[writeOffset++] = meshPortTextMessageApp;
  outBuffer[writeOffset++] = 0x12;
  outBuffer[writeOffset++] = (uint8_t)textLen;

  memcpy(outBuffer + writeOffset, textBytes, textLen);
  writeOffset += textLen;

  return writeOffset;
}

// ---- protobuf parsing for Data{portnum, payload} ----
bool MeshRadio::pbReadVarint(const uint8_t*& cursor, const uint8_t* end, uint64_t& valueOut) {
  valueOut = 0;
  uint8_t shift = 0;

  while (cursor < end && shift <= 63) {
    const uint8_t byte = *cursor++;
    valueOut |= (uint64_t)(byte & 0x7F) << shift;
    if (!(byte & 0x80)) {
      return true;
    }
    shift += 7;
  }
  return false;
}

bool MeshRadio::pbSkipField(const uint8_t*& cursor, const uint8_t* end, uint8_t wireType) {
  uint64_t lengthOrValue = 0;

  switch (wireType) {
    case 0:  // varint
      return pbReadVarint(cursor, end, lengthOrValue);

    case 1:  // 64-bit
      if ((size_t)(end - cursor) < 8) return false;
      cursor += 8;
      return true;

    case 2:  // length-delimited
      if (!pbReadVarint(cursor, end, lengthOrValue)) return false;
      if ((uint64_t)(end - cursor) < lengthOrValue) return false;
      cursor += (size_t)lengthOrValue;
      return true;

    case 5:  // 32-bit
      if ((size_t)(end - cursor) < 4) return false;
      cursor += 4;
      return true;

    default:
      return false;
  }
}

bool MeshRadio::parseDataText(const uint8_t* protobufBytes, size_t protobufLen,
                              uint32_t& portnumOut, const uint8_t*& payloadOut, size_t& payloadLenOut) {
  portnumOut = 0;
  payloadOut = nullptr;
  payloadLenOut = 0;

  const uint8_t* cursor = protobufBytes;
  const uint8_t* end = protobufBytes + protobufLen;

  while (cursor < end) {
    uint64_t key = 0;
    if (!pbReadVarint(cursor, end, key)) {
      return false;
    }

    const uint32_t fieldNumber = (uint32_t)(key >> 3);
    const uint8_t wireType = (uint8_t)(key & 0x07);

    if (fieldNumber == 1 && wireType == 0) {
      uint64_t portnumU64 = 0;
      if (!pbReadVarint(cursor, end, portnumU64)) return false;
      portnumOut = (uint32_t)portnumU64;
      continue;
    }

    if (fieldNumber == 2 && wireType == 2) {
      uint64_t payloadLen = 0;
      if (!pbReadVarint(cursor, end, payloadLen)) return false;
      if ((uint64_t)(end - cursor) < payloadLen) return false;

      payloadOut = cursor;
      payloadLenOut = (size_t)payloadLen;
      cursor += payloadLenOut;
      continue;
    }

    if (!pbSkipField(cursor, end, wireType)) {
      return false;
    }
  }

  return true;
}
