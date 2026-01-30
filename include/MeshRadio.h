#pragma once

#include <Arduino.h>

class MeshRadio {
public:
  struct Config {
    const char* channelName = nullptr;
    const char* channelKeyBase64 = nullptr;

    uint8_t macAddress[6] = {};

    float frequencyMHz = 869.525F;
    float bandwidthKHz = 250.0F;
    uint8_t spreadingFactor = 9;
    uint8_t codingRate = 5;
    uint8_t syncWord = 0x2B;
    uint16_t preambleLength = 16;
    float tcxoVoltage = 1.6F;

    int8_t outputPowerDbm = 22;
    float currentLimitmA = 140.0F;

    uint8_t hopLimit = 3;
    bool wantAck = false;
    bool viaMqtt = false;
    uint8_t hopStart = 3;
  };

  using TextRxCallback = void (*)(uint32_t fromNodeId, const char* text);

  MeshRadio() = default;

  bool begin(const Config& config);
  void pollRx();
  bool sleep();

  bool txText(const char* text);
  bool txText(const String& text) {
    return txText(text.c_str());
  }

  void setTextRxCallback(TextRxCallback callback) {
    textRxCallback = callback;
  }

  uint32_t getNodeId() const {
    return nodeId;
  }
  uint8_t getChannelHash() const {
    return channelHash;
  }
  int getLastError() const {
    return lastError;
  }

private:
  // internal helpers
  static bool decodeBase64Key32(const char* base64Key, uint8_t decodedKey[32]);
  static uint32_t getMeshtasticNodeIdFromMac(const uint8_t macBytes[6]);
  static uint8_t xorHashBytes(const uint8_t* bytes, size_t length);
  static uint8_t computeChannelHashByte(const char* channelName, const uint8_t keyBytes[32]);
  static uint8_t makeMeshFlags(uint8_t hopLimit, bool wantAck, bool viaMqtt, uint8_t hopStart);

  static void aesCtrCryptMeshtastic(uint8_t* buffer, size_t bufferLen,
                                    uint32_t packetId, uint32_t fromNode,
                                    const uint8_t keyBytes[32]);

  static inline void putLe32(uint8_t* dst, uint32_t value);
  static inline uint32_t getLe32(const uint8_t* src);

  static size_t buildMeshPacket(uint8_t* outFrame, size_t outFrameMax,
                                uint32_t toNode, uint32_t fromNode, uint32_t packetId,
                                uint8_t flags, uint8_t channelHashByte,
                                const uint8_t* encryptedPayload, size_t encryptedPayloadLen);

  static size_t buildDataProtobufText(uint8_t* outBuffer, size_t outBufferMax,
                                      const uint8_t* textBytes, size_t textLen);

  static bool pbReadVarint(const uint8_t*& cursor, const uint8_t* end, uint64_t& valueOut);
  static bool pbSkipField(const uint8_t*& cursor, const uint8_t* end, uint8_t wireType);
  static bool parseDataText(const uint8_t* protobufBytes, size_t protobufLen,
                            uint32_t& portnumOut, const uint8_t*& payloadOut, size_t& payloadLenOut);

private:
  Config config{};
  TextRxCallback textRxCallback = nullptr;

  uint8_t channelKey[32] = {};
  uint8_t channelHash = 0;
  uint32_t nodeId = 0;
  uint32_t packetIdCounter = 0;

  int lastError = 0;
};
