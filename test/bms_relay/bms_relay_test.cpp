#include "bms_relay.h"

#include <unity.h>

#include <cstdio>
#include <deque>
#include <memory>

#include "packet.h"

std::unique_ptr<BmsRelay> relay;
std::deque<int> mockBmsData;
std::vector<uint8_t> mockDataOut;
unsigned long timeMillis = 0;

void setUp(void) {
  relay.reset(new BmsRelay(
      [&]() {
        if (mockBmsData.empty()) {
          return -1;
        }
        int data = mockBmsData.front();
        mockBmsData.pop_front();
        return data;
      },
      [&](uint8_t b) {
        mockDataOut.push_back(b);
        return 1;
      },
      [&]() { return timeMillis; }));
  mockBmsData.clear();
  mockDataOut.clear();
}

void addMockData(const std::vector<uint8_t>& data) {
  mockBmsData.insert(mockBmsData.end(), data.begin(), data.end());
}

void expectDataOut(const std::vector<uint8_t>& expected) {
  TEST_ASSERT_TRUE(expected == mockDataOut);
}

void testUnknownBytesGetsForwardedImmediately(void) {
  addMockData({0x1, 0x2, 0x3});
  relay->loop();
  TEST_ASSERT_TRUE(mockBmsData.empty());
  expectDataOut({0x1, 0x2, 0x3});
}

void testUnknownDataAfterKnownPacketGetsFlushedImmediately(void) {
  addMockData({0xFF, 0x55, 0xAA, 0x6, 0x1, 0x2, 0x3, 0x4, 0x2, 0xE, 0x1});
  relay->loop();
  TEST_ASSERT_TRUE(mockBmsData.empty());
  expectDataOut({0xFF, 0x55, 0xAA, 0x6, 0x1, 0x2, 0x3, 0x4, 0x2, 0xE, 0x1});
}

void testSerialGetsRecordedAndIntercepted(void) {
  addMockData(
      {0x1, 0x2, 0x3, 0xFF, 0x55, 0xAA, 0x6, 0x1, 0x2, 0x3, 0x4, 0x2, 0xE});
  relay->setBMSSerialOverride(0x8040201);
  relay->loop();
  TEST_ASSERT_TRUE(mockBmsData.empty());
  TEST_ASSERT_EQUAL(0x1020304, relay->getCapturedBMSSerial());
  // Expect no data so far because we identify packets only when we see the
  // header of the next one.
  expectDataOut(
      {0x1, 0x2, 0x3, 0xFF, 0x55, 0xAA, 0x6, 0x8, 0x4, 0x2, 0x1, 0x2, 0x13});
}

void testPacketLengths() {
  std::vector<std::vector<uint8_t>> packets = {
      {0xff, 0x55, 0xaa, 0x00, 0x80, 0x02, 0x7e},
      {0xff, 0x55, 0xaa, 0x02, 0x0f, 0x28, 0x0f, 0x2c, 0x0f, 0x2b,
       0x0f, 0x29, 0x0f, 0x2a, 0x0f, 0x2b, 0x0f, 0x2a, 0x0f, 0x2c,
       0x0f, 0x29, 0x0f, 0x2b, 0x0f, 0x29, 0x0f, 0x2a, 0x0f, 0x22,
       0x0f, 0x2a, 0x0f, 0x2a, 0x00, 0x2a, 0x05, 0x7b},
      {0xff, 0x55, 0xaa, 0x03, 0x29, 0x02, 0x2a},
      {0xff, 0x55, 0xaa, 0x04, 0x16, 0x17, 0x17, 0x17, 0x18, 0x02, 0x75},
      {0xff, 0x55, 0xaa, 0x05, 0x00, 0x01, 0x02, 0x04},
      {0xFF, 0x55, 0xAA, 0x6, 0x8, 0x4, 0x2, 0x1, 0x2, 0x13},
      {0xff, 0x55, 0xaa, 0x07, 0x10, 0xcc, 0x10, 0x57, 0x09, 0xc4, 0x50, 0x04,
       0x65},
      {0xff, 0x55, 0xaa, 0x08, 0x06, 0x02, 0x0c},
      {0xff, 0x55, 0xaa, 0x09, 0x03, 0x02, 0x0a},
      {0xff, 0x55, 0xaa, 0x0b, 0x0b, 0xc0, 0x02, 0xd4},
      {0xff, 0x55, 0xaa, 0x0c, 0x00, 0x00, 0x02, 0x0a},
      {0xff, 0x55, 0xaa, 0x0d, 0x02, 0xda, 0x47, 0x03, 0x2e},
      {0xff, 0x55, 0xaa, 0x0f, 0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0f},
      {0xff, 0x55, 0xaa, 0x10, 0x03, 0x03, 0x0b, 0x03, 0x03, 0x03, 0x03, 0x03,
       0x03, 0x03, 0x02, 0x34},
      {0xff, 0x55, 0xaa, 0x11, 0x00, 0x00, 0x00, 0x00, 0x02, 0x0f}};
  std::vector<uint8_t> receivedPacket;
  relay->addReceivedPacketCallback([&](BmsRelay*, Packet* p) {
    TEST_ASSERT_TRUE(p->isValid());
    receivedPacket = std::vector<uint8_t>(p->start(), p->start() + p->len());
  });
  for (const auto& packet : packets) {
    addMockData(packet);
    relay->loop();
    TEST_ASSERT_EQUAL(packet.size(), receivedPacket.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&packet[0], &receivedPacket[0],
                                  packet.size());
  }
}

void testPacketCallback() {
  addMockData({0x1, 0x2, 0x3, 0xFF, 0x55, 0xAA, 0x6, 0x1, 0x2, 0x3, 0x4, 0x2,
               0xE, 0xFF, 0x55, 0xAA});
  std::vector<uint8_t> receivedPacket;
  relay->addReceivedPacketCallback([&](BmsRelay*, Packet* p) {
    const uint8_t* start = p->start();
    receivedPacket.assign(start, start + p->len());
  });
  relay->loop();
  std::vector<uint8_t> expected(
      {0xFF, 0x55, 0xAA, 0x6, 0x1, 0x2, 0x3, 0x4, 0x2, 0xE});
  TEST_ASSERT_EQUAL(expected.size(), receivedPacket.size());
  for (int i = 0; i < expected.size(); i++) {
    TEST_ASSERT_EQUAL(expected[i], receivedPacket[i]);
  }
}

void testBatterySocParsing() {
  addMockData({0xFF, 0x55, 0xAA, 0x3, 0x2B, 0x02, 0x2C, 0xFF, 0x55, 0xAA});
  relay->loop();
  TEST_ASSERT_EQUAL(43, relay->getBmsReportedSOC());
}

void testCurrentParsing() {
  addMockData({0xff, 0x55, 0xaa, 0x5, 0xff, 0xe8, 0x3, 0xea, 0xFF, 0x55, 0xAA});
  relay->loop();
  TEST_ASSERT_FLOAT_WITHIN(0.01, -1.32, relay->getCurrentInAmps());
}

void testCellVoltageParsing() {
  addMockData({0xff, 0x55, 0xaa, 0x02, 0x0f, 0x14, 0x0f, 0x14, 0x0f, 0x14, 0x0f,
               0x13, 0x0f, 0x14, 0x0f, 0x14, 0x0f, 0x14, 0x0f, 0x13, 0x0f, 0x14,
               0x0f, 0x13, 0x0f, 0x13, 0x0f, 0x13, 0x0f, 0x13, 0x0f, 0x14, 0x0f,
               0x14, 0x00, 0x2a, 0x04, 0x31, 0xFF, 0x55, 0xAA});
  relay->loop();
  uint16_t expected[15] = {3860, 3860, 3860, 3859, 3860, 3860, 3860, 3859,
                           3860, 3859, 3859, 3859, 3859, 3860, 3860};
  TEST_ASSERT_EQUAL_INT16_ARRAY(expected, relay->getCellMillivolts(), 15);
  TEST_ASSERT_EQUAL(57894, relay->getTotalVoltageMillivolts());
}

void testTemperatureParsing() {
  addMockData({0xff, 0x55, 0xaa, 0x04, 0x13, 0x14, 0x14, 0x14, 0x16, 0x02, 0xFF,
               0x55, 0xAA});
  relay->loop();
  int8_t expected[5] = {19, 20, 20, 20, 22};
  TEST_ASSERT_EQUAL_INT8_ARRAY(expected, relay->getTemperaturesCelsius(), 5);
  TEST_ASSERT_EQUAL(20, relay->getAverageTemperatureCelsius());
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(testUnknownDataAfterKnownPacketGetsFlushedImmediately);
  RUN_TEST(testPacketLengths);
  RUN_TEST(testSerialGetsRecordedAndIntercepted);
  RUN_TEST(testUnknownBytesGetsForwardedImmediately);
  RUN_TEST(testPacketCallback);
  RUN_TEST(testBatterySocParsing);
  RUN_TEST(testCurrentParsing);
  RUN_TEST(testCellVoltageParsing);
  UNITY_END();

  return 0;
}