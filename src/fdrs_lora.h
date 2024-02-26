
#include <RadioLib.h>

// Internal Globals
// Default values: overridden by settings in config, if present

#define GLOBAL_ACK_TIMEOUT 400 // LoRa ACK timeout in ms. (Minimum = 200)
#define GLOBAL_LORA_RETRIES 2  // LoRa ACK automatic retries [0 - 3]
#define GLOBAL_LORA_TXPWR 17   // LoRa TX power in dBm (: +2dBm - +17dBm (for SX1276-7) +20dBm (for SX1278))

#define TXWINDOWMS 300
#define SPBUFFSIZE 10
#define DRBUFFSIZE lora_size
#define DRQUEUEEMPTY ((startIdxDR == endIdxDR) ? true: false)
#define SPQUEUEEMPTY ((startIdxSP == endIdxSP) ? true: false)
#define DRQUEUEFULL (((startIdxDR + 1) % DRBUFFSIZE) == endIdxDR ? true: false)
#define SPQUEUEFULL (((startIdxSP + 1) % SPBUFFSIZE) == endIdxSP ? true: false)

// select LoRa band configuration
#if defined(LORA_FREQUENCY)
#define FDRS_LORA_FREQUENCY LORA_FREQUENCY
#else
#define FDRS_LORA_FREQUENCY GLOBAL_LORA_FREQUENCY
#endif // LORA_FREQUENCY

// select LoRa SF configuration
#if defined(LORA_SF)
#define FDRS_LORA_SF LORA_SF
#else
#define FDRS_LORA_SF GLOBAL_LORA_SF
#endif // LORA_SF

// select LoRa ACK configuration
#if defined(LORA_ACK) || defined(GLOBAL_LORA_ACK)
#define FDRS_LORA_ACK
#endif // LORA_ACK

// select LoRa ACK Timeout configuration
#if defined(LORA_ACK_TIMEOUT)
#define FDRS_ACK_TIMEOUT LORA_ACK_TIMEOUT
#else
#define FDRS_ACK_TIMEOUT GLOBAL_ACK_TIMEOUT
#endif // LORA_ACK_TIMEOUT

// select LoRa Retry configuration
#if defined(LORA_RETRIES)
#define FDRS_LORA_RETRIES LORA_RETRIES
#else
#define FDRS_LORA_RETRIES GLOBAL_LORA_RETRIES
#endif // LORA_RETRIES

// select  LoRa Tx Power configuration
#if defined(LORA_TXPWR)
#define FDRS_LORA_TXPWR LORA_TXPWR
#else
#define FDRS_LORA_TXPWR GLOBAL_LORA_TXPWR
#endif // LORA_TXPWR

// select  LoRa BANDWIDTH configuration
#if defined(LORA_BANDWIDTH)
#define FDRS_LORA_BANDWIDTH LORA_BANDWIDTH
#else
#define FDRS_LORA_BANDWIDTH GLOBAL_LORA_BANDWIDTH
#endif // LORA_BANDWIDTH

// select  LoRa Coding Rate configuration
#if defined(LORA_CR)
#define FDRS_LORA_CR LORA_CR
#else
#define FDRS_LORA_CR GLOBAL_LORA_CR
#endif // LORA_CR

// select  LoRa SyncWord configuration
#if defined(LORA_SYNCWORD)
#define FDRS_LORA_SYNCWORD LORA_SYNCWORD
#else
#define FDRS_LORA_SYNCWORD GLOBAL_LORA_SYNCWORD
#endif // LORA_SYNCWORD

// select  LoRa Release Interval configuration
#if defined(LORA_INTERVAL)
#define FDRS_LORA_INTERVAL LORA_INTERVAL
#else
#define FDRS_LORA_INTERVAL GLOBAL_LORA_INTERVAL
#endif // LORA_INTERVAL

#ifndef LORA_BUSY
#define LORA_BUSY RADIOLIB_NC
#endif

#ifdef CUSTOM_SPI
#ifdef ARDUINO_ARCH_RP2040
RADIOLIB_MODULE radio = new Module(LORA_SS, LORA_DIO, LORA_RST, LORA_BUSY, SPI1);
#endif  // RP2040
RADIOLIB_MODULE radio = new Module(LORA_SS, LORA_DIO, LORA_RST, LORA_BUSY, SPI);
#else
RADIOLIB_MODULE radio = new Module(LORA_SS, LORA_DIO, LORA_RST, LORA_BUSY);
#endif  // CUSTOM_SPI

#ifdef LORA_ACK
  bool ack = true;
#else
  bool ack = false;
#endif // LORA_ACK

LoRaPing loraPing;

const uint8_t lora_size = 250 / sizeof(DataReading);

tskLoRaTaskSP loraSPBuffTx[SPBUFFSIZE];
tskLoRaTaskDR loraDRBuffTx[DRBUFFSIZE];
int loraTxState = stReady;
int loraAckState = stReady;
int startIdxDR = 0;
int endIdxDR = 0;
int startIdxSP = 0;
int endIdxSP = 0;


#ifdef FDRS_GATEWAY
    #ifndef USE_ESPNOW   // mac_prefix used for both ESP-NOW and LoRa - avoid redefinition warnings
        const uint8_t mac_prefix[] = {MAC_PREFIX};
        const uint8_t selfAddress[] = {MAC_PREFIX, UNIT_MAC};
    #endif
    uint16_t LoRa1 = ((mac_prefix[4] << 8) | LORA_NEIGHBOR_1); // Use 2 bytes for LoRa addressing instead of previous 3 bytes
    uint16_t LoRa2 = ((mac_prefix[4] << 8) | LORA_NEIGHBOR_2);
    uint16_t loraBroadcast = 0xFFFF;
    uint16_t gtwyAddress = ((mac_prefix[4] << 8) | UNIT_MAC); // for a gateway this is our own address
#elif defined(FDRS_NODE)
    uint8_t selfAddress[6] = {0};
    uint16_t LoRa1 = 0;
    uint16_t LoRa2 = 0;
    uint16_t loraBroadcast = 0;
    const uint8_t mac_prefix[] = {MAC_PREFIX};
    uint16_t gtwyAddress = ((mac_prefix[4] << 8) | GTWY_MAC);   // for a node, this is our gateway
#endif // FDRS_GATEWAY

volatile bool transmitFlag = false;            // flag to indicate transmission or reception state
volatile bool enableInterrupt = true; // disable interrupt when it's not needed
volatile bool operationDone = false;  // flag to indicate that a packet was sent or received
unsigned long receivedLoRaMsg = 0; // Number of total LoRa packets destined for us and of valid size
unsigned long ackOkLoRaMsg = 0;    // Number of total LoRa packets with valid CRC
extern time_t now;
unsigned long transmitLoRaMsgwAck = 0; // Number of total LoRa packets destined for us and of valid size
unsigned long msgOkLoRa = 0;           // Number of total LoRa packets with valid CRC
time_t netTimeOffset = UINT32_MAX;  // One direction of LoRa Ping time in units of seconds (1/2 full ping time)
unsigned long tx_start_time = 0;

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void setFlag(void)
{
    // check if the interrupt is enabled
    if (!enableInterrupt)
    {
        return;
    }
    operationDone = true; // we sent or received  packet, set the flag
}

// crc16_update used by both LoRa and filesystem

// CRC16 from https://github.com/4-20ma/ModbusMaster/blob/3a05ff87677a9bdd8e027d6906dc05ca15ca8ade/src/util/crc16.h#L71

/** @ingroup util_crc16
    Processor-independent CRC-16 calculation.
    Polynomial: x^16 + x^15 + x^2 + 1 (0xA001)<br>
    Initial value: 0xFFFF
    This CRC is normally used in disk-drive controllers.
    @param uint16_t crc (0x0000..0xFFFF)
    @param uint8_t a (0x00..0xFF)
    @return calculated CRC (0x0000..0xFFFF)
*/

static uint16_t crc16_update(uint16_t crc, uint8_t a)
{
  int i;

  crc ^= a;
  for (i = 0; i < 8; ++i)
  {
    if (crc & 1)
      crc = (crc >> 1) ^ 0xA001;
    else
      crc = (crc >> 1);
  }

  return crc;
}

void printLoraPacket(uint8_t *p, int size)
{
  printf("Printing packet of size %d.", size);
  for (int i = 0; i < size; i++)
  {
    if (i % 2 == 0)
      printf("\n%02d: ", i);
    printf("%02X ", p[i]);
  }
  printf("\n");
}

// Do not call this function directly, instead call transmitLoRaAsync(...)
// Transmits Lora data by calling RadioLib library function
// Returns void becuase function is Async and does not receive any data
void transmitLoRa(uint16_t *destMac, DataReading *packet, uint8_t len)
{
    uint8_t pkt[6 + (len * sizeof(DataReading))];
    uint16_t calcCRC = 0x0000;
    loraTxState = stInProcess;
    // Building packet -- address portion - first 4 bytes
    pkt[0] = (*destMac >> 8);                           // high byte of destination MAC
    pkt[1] = (*destMac & 0x00FF);                       // low byte of destination MAC
    pkt[2] = selfAddress[4];                            // high byte of source MAC (ourselves)
    pkt[3] = selfAddress[5];                            // low byte of source MAC
    // Building packet -- data portion - 7 bytes
    memcpy(&pkt[4], packet, len * sizeof(DataReading)); // copy data portion of packet
    // Calculate CRC of address and data portion of the packet
    // Last 2 bytes are CRC so do not include them in the calculation itself
    for (int i = 0; i < (sizeof(pkt) - 2); i++)
    {
        // printf("CRC: %02X : %d\n",calcCRC, i);
        calcCRC = crc16_update(calcCRC, pkt[i]);
    }
    if(!ack) {
        calcCRC = crc16_update(calcCRC, 0xA1); // Recalculate CRC for No ACK
    }
    pkt[len * sizeof(DataReading) + 4] = (calcCRC >> 8); // Append calculated CRC to the last 2 bytes of the packet
    pkt[len * sizeof(DataReading) + 5] = (calcCRC & 0x00FF);
    DBG("Sending LoRa DR");
    DBG1("Transmitting LoRa message of size " + String(sizeof(pkt)) + " bytes with CRC 0x" + String(calcCRC, HEX) + " to destination 0x" + String(*destMac, HEX));
    // printLoraPacket(pkt,sizeof(pkt));
    tx_start_time = millis();
    int state = radio.startTransmit(pkt, sizeof(pkt));
    transmitFlag = true;
    if (state != RADIOLIB_ERR_NONE)
    {
        DBG("Xmit failed, code " + String(state));
        while (true)
            ;
    }
    return;
}

// For now SystemPackets will not use ACK but will calculate CRC
// Returns CRC_NULL as SystemPackets do not use ACKS at current time
void transmitLoRa(uint16_t *destMac, SystemPacket *packet, uint8_t len)
{
    uint8_t pkt[6 + (len * sizeof(SystemPacket))];
    uint16_t calcCRC = 0x0000;
    loraTxState = stInProcess;
    // Building packet -- address portion - first 4 bytes
    pkt[0] = (*destMac >> 8);                            // high byte of destination MAC
    pkt[1] = (*destMac & 0x00FF);                        // low byte of destination MAC
    pkt[2] = selfAddress[4];                             // high byte of source MAC (ourselves)
    pkt[3] = selfAddress[5];                             // low byte of source MAC
    // Building packet -- data portion - 5 bytes
    memcpy(&pkt[4], packet, len * sizeof(SystemPacket)); // copy data portion of packet
    // Calculate CRC of address and data portion of the packet
    // Last 2 bytes are CRC so do not include them in the calculation itself
    for (int i = 0; i < (sizeof(pkt) - 2); i++)
    {
        // printf("CRC: %02X : %d\n",calcCRC, i);
        calcCRC = crc16_update(calcCRC, pkt[i]);
    }
    calcCRC = crc16_update(calcCRC, 0xA1); // Recalculate CRC for No ACK
    // Building packet -- adding CRC - last 2 bytes
    pkt[len * sizeof(SystemPacket) + 4] = (calcCRC >> 8);
    pkt[len * sizeof(SystemPacket) + 5] = (calcCRC & 0x00FF);
    // Packet is constructed now transmit the packet
    DBG1("Transmitting LoRa message of size " + String(sizeof(pkt)) + " bytes with CRC 0x" + String(calcCRC, HEX) + " to destination 0x" + String(*destMac, HEX));
    // printLoraPacket(pkt,sizeof(pkt));
    tx_start_time = millis();
    int state = radio.startTransmit(pkt, sizeof(pkt));
    transmitFlag = true;
    if (state != RADIOLIB_ERR_NONE)
    {
        DBG("Xmit failed, code " + String(state));
        while (true)
            ;
    }
    return;
}

void begin_lora()
{
#ifdef CUSTOM_SPI
#ifdef ESP32
  SPI.begin(LORA_SPI_SCK, LORA_SPI_MISO, LORA_SPI_MOSI);
#endif  // ESP32
#ifdef ARDUINO_ARCH_RP2040
  SPI1.setRX(LORA_SPI_MISO);
  SPI1.setTX(LORA_SPI_MOSI);
  SPI1.setSCK(LORA_SPI_SCK);
  SPI1.begin(false);
#endif  //ARDUINO_ARCH_RP2040
#endif  // CUSTOM_SPI

#ifdef USE_SX126X
  int state = radio.begin(FDRS_LORA_FREQUENCY, FDRS_LORA_BANDWIDTH, FDRS_LORA_SF, FDRS_LORA_CR, FDRS_LORA_SYNCWORD, FDRS_LORA_TXPWR, 8, 1.6, false);
#else
  int state = radio.begin(FDRS_LORA_FREQUENCY, FDRS_LORA_BANDWIDTH, FDRS_LORA_SF, FDRS_LORA_CR, FDRS_LORA_SYNCWORD, FDRS_LORA_TXPWR, 8, 0);
#endif

    if (state == RADIOLIB_ERR_NONE)
    {
        DBG("RadioLib initialization successful!");
    }
    else
    {
        DBG("RadioLib initialization failed, code " + String(state));
        while (true)
            ;
    }
#ifdef USE_SX126X
    radio.setDio1Action(setFlag);
#else
    radio.setDio0Action(setFlag, RISING);
#endif
    radio.setCRC(false);
    DBG("LoRa Initialized. Frequency: " + String(FDRS_LORA_FREQUENCY) + "  Bandwidth: " + String(FDRS_LORA_BANDWIDTH) + "  SF: " + String(FDRS_LORA_SF) + "  CR: " + String(FDRS_LORA_CR) + "  SyncWord: " + String(FDRS_LORA_SYNCWORD) + "  Tx Power: " + String(FDRS_LORA_TXPWR) + "dBm");
#ifdef FDRS_NODE
    selfAddress[4] = radio.randomByte();
    selfAddress[5] = radio.randomByte();
    DBG("LoRa node address is 0x" + String(selfAddress[4], HEX) + String(selfAddress[5], HEX));
#endif
    state = radio.startReceive(); // start listening for LoRa packets
    if (state != RADIOLIB_ERR_NONE)
    {
        DBG(" failed, code " + String(state));
        while (true)
            ;
    }
}

bool transmitLoRaAsync(uint16_t *destMAC, SystemPacket *packet, uint8_t len)
{   
    for(int i=0; i < len; i++)
    {
        //check for full buffer
        if(SPQUEUEFULL) {
            DBG("Lora SP Buffer Overflow!");
            return false;
        }
    
        //add packet to buffer
        loraSPBuffTx[startIdxSP].data = *(packet + i);
        loraSPBuffTx[startIdxSP].dstAddress = *destMAC;
        startIdxSP = (startIdxSP + 1) % SPBUFFSIZE;
    }
    DBG2("SP added to LoRa buffer. start: " + String(startIdxSP) + " end: " + String(endIdxSP));
    return true;
}

// Wrapper for transmitLoRa for DataReading type packets to handle processing Receiving CRCs and retransmitting packets
bool transmitLoRaAsync(uint16_t *destMAC, DataReading *packet, uint8_t len)
{
    // we need to prevent modifying index while waiting 
    // for an ack
    if(loraAckState == stReady) {
        //add packet to buffer
        for(int i=0; i < len; i++)
        {
            if(DRQUEUEFULL)
            {
                DBG("Lora DR Buffer Overflow!");
                return false;
            }
            loraDRBuffTx[startIdxDR].data = *(packet + i);
            loraDRBuffTx[startIdxDR].dstAddress = *destMAC;
            startIdxDR = (startIdxDR + 1) % DRBUFFSIZE;
        }
        DBG2("DR added to LoRa buffer. start: " + String(startIdxDR) + " end: " + String(endIdxDR));
        return true;
    }
    else {
        // JL - do we internally store the start index, check for space, and then
        // adjust the index once the acks are completed???
        DBG2("Unable to add DR to LoRa buffer due to pending ACK.");
        return false;
    }
}

// Send time to LoRa broadcast and peers
// Only used in gateways
void sendTimeLoRa() {
    // Check for node or gateway.  Do not send if we are a node.
  if(selfAddress[0] != 0) {
    DBG1("Sending time via LoRa");
    SystemPacket spTimeLoRa = {.cmd = cmd_time, .param = now};
    DBG1("Sending time to LoRa broadcast");
    transmitLoRaAsync(&loraBroadcast, &spTimeLoRa, 1);
    // Do not send to LoRa peers if their address is 0x..00
    if(((LoRa1 & 0x00FF) != 0x0000) && (LoRa1 != timeSource.tmAddress)) {
        DBG1("Sending time to LoRa Neighbor 1");
        spTimeLoRa.param = now;
        // add LoRa neighbor 1
        transmitLoRaAsync(&LoRa1, &spTimeLoRa, 1);
    }
    if(((LoRa2 & 0x00FF) != 0x0000) && (LoRa2 != timeSource.tmAddress)) {
        DBG1("Sending time to LoRa Neighbor 2");
        spTimeLoRa.param = now;
        // add LoRa neighbor 2
        transmitLoRaAsync(&LoRa2, &spTimeLoRa, 1);
    }
  }
  return;
}

// Send time to LoRa node at specific address
void sendTimeLoRa(uint16_t addr) {
  
  SystemPacket spTimeLoRa = {.cmd = cmd_time, .param = now};
  DBG1("Sending time to LoRa address 0x" + String(addr,HEX));
  transmitLoRaAsync(&addr, &spTimeLoRa, 1);
  return;
}

// FDRS Sensor pings address and listens for a defined amount of time for a reply
bool pingRequestLoRa(uint16_t address, uint32_t timeout)
{

    if(loraPing.status == stReady) {
        SystemPacket sys_packet = {.cmd = cmd_ping, .param = ping_request};

        loraPing.timeout = timeout;
        loraPing.status = stInProcess;
        loraPing.address = address;
        loraPing.start = millis();
        if(loraTxState == stReady) {
            transmitLoRa(&address,&sys_packet,1);
            DBG1("LoRa ping request sent to address: 0x" + String(address, HEX));
            return true;
        }
        else {
            if(transmitLoRaAsync(&address, &sys_packet, 1))
            {
                DBG1("LoRa ping request queued to address: 0x" + String(address, HEX));
                return true;
            }
            else {
                DBG1("Error sending LoRa ping.");
            }
        }
    }
    return false;
}
// FDRS sends ping reply
bool pingReplyLoRa(uint16_t address)
{
        SystemPacket sys_packet = {.cmd = cmd_ping, .param = ping_reply};

        if(loraTxState == stReady) {
            transmitLoRa(&address,&sys_packet,1);
            DBG1("LoRa ping reply sent to address: 0x" + String(address, HEX));
            return true;
        }
        else {
            if(transmitLoRaAsync(&address, &sys_packet, 1))
            {
                DBG1("LoRa ping reply queued to address: 0x" + String(address, HEX));
                return true;
            }
            else {
                DBG1("Error sending LoRa ping.");
            }
        }
    return false;
}

// ****DO NOT CALL receiveLoRa() directly! *****   Call handleLoRa() instead!
// receiveLoRa for Sensors
//  USED to get ACKs (SystemPacket type) from LoRa gateway at this point.  May be used in the future to get other data
// Return type is crcResult struct - CRC_OK, CRC_BAD, CRC_NULL.  CRC_NULL used for non-ack data

crcResult receiveLoRa()
{
    int packetSize = radio.getPacketLength();
    if ((((packetSize - 6) % sizeof(DataReading) == 0) || ((packetSize - 6) % sizeof(SystemPacket) == 0)) && packetSize > 0)
    { // packet size should be 6 bytes plus multiple of size of DataReading
        uint8_t packet[packetSize];
        uint16_t packetCRC = 0x0000; // CRC Extracted from received LoRa packet
        uint16_t calcCRC = 0x0000;   // CRC calculated from received LoRa packet
        uint16_t sourceMAC = 0x0000;
        uint16_t destMAC = 0x0000;

        radio.readData((uint8_t *)&packet, packetSize);

        destMAC = (packet[0] << 8) | packet[1];
        sourceMAC = (packet[2] << 8) | packet[3];
        packetCRC = ((packet[packetSize - 2] << 8) | packet[packetSize - 1]);
        // Print all packets
        // DBG2("Source Address: 0x" + String(packet[2], HEX) + String(packet[3], HEX) + " Destination Address: 0x" + String(packet[0], HEX) + String(packet[1], HEX));
        
#ifdef FDRS_GATEWAY
        // for gateway - only listen to our own address
        if (destMAC == (selfAddress[4] << 8 | selfAddress[5]))
#elif defined(FDRS_NODE)
        // for Node - need to listen to broadcast
        if (destMAC == (selfAddress[4] << 8 | selfAddress[5]) || (destMAC == 0xFFFF))
#endif
        { // Check if addressed to this device or broadcast
            // printLoraPacket(packet,sizeof(packet));
            if (receivedLoRaMsg != 0)
            { // Avoid divide by 0
                DBG1("Incoming LoRa. Size: " + String(packetSize) + " Bytes, RSSI: " + String(radio.getRSSI()) + "dBm, SNR: " + String(radio.getSNR()) + "dB, PacketCRC: 0x" + String(packetCRC, HEX) + ", Total LoRa received: " + String(receivedLoRaMsg) + ", CRC Ok Pct " + String((float)ackOkLoRaMsg / receivedLoRaMsg * 100) + "%");
            }
            else
            {
                DBG1("Incoming LoRa. Size: " + String(packetSize) + " Bytes, RSSI: " + String(radio.getRSSI()) + "dBm, SNR: " + String(radio.getSNR()) + "dB, PacketCRC: 0x" + String(packetCRC, HEX) + ", Total LoRa received: " + String(receivedLoRaMsg));
            }
            receivedLoRaMsg++;
            // Evaluate CRC
            for (int i = 0; i < (packetSize - 2); i++)
            { // Last 2 bytes of packet are the CRC so do not include them in calculation
                // printf("CRC: %02X : %d\n",calcCRC, i);
                calcCRC = crc16_update(calcCRC, packet[i]);
            }
            if ((packetSize - 6) % sizeof(DataReading) == 0)
            { // DataReading type packet
                if (calcCRC == packetCRC)
                {   // We've received a DR and sending an ACK 
                    SystemPacket ACK = {.cmd = cmd_ack, .param = CRC_OK};
                    DBG1("CRC Match, sending ACK packet to node 0x" + String(sourceMAC, HEX) + "(hex)");
                    transmitLoRaAsync(&sourceMAC, &ACK, 1); // Send ACK back to source
                }
                else if (packetCRC == crc16_update(calcCRC, 0xA1))
                { // Sender does not want ACK and CRC is valid
                    DBG1("Node address 0x" + String(sourceMAC, 16) + " does not want ACK");
                }
                else
                {   // We've received a DR and CRC is bad
                    SystemPacket NAK = {.cmd = cmd_ack, .param = CRC_BAD};
                    // Send NAK packet
                    DBG1("CRC Mismatch! Packet CRC is 0x" + String(packetCRC, HEX) + ", Calculated CRC is 0x" + String(calcCRC, HEX) + " Sending NAK packet to node 0x" + String(sourceMAC, HEX) + "(hex)");
                    transmitLoRaAsync(&sourceMAC, &NAK, 1); // CRC did not match so send NAK to source
                    newData = event_clear;             // do not process data as data may be corrupt
                    return CRC_BAD;                    // Exit function and do not update newData to send invalid data further on
                }
                memcpy(&theData, &packet[4], packetSize - 6); // Split off data portion of packet (N - 6 bytes (6 bytes for headers and CRC))
                ln = (packetSize - 6) / sizeof(DataReading);
                ackOkLoRaMsg++;
                if (memcmp(&sourceMAC, &LoRa1, 2) == 0)
                { // Check if it is from a registered sender
                    newData = event_lora1;
                    return CRC_OK;
                }
                if (memcmp(&sourceMAC, &LoRa2, 2) == 0)
                {
                    newData = event_lora2;
                    return CRC_OK;
                }
                newData = event_lorag;
                return CRC_OK;
            }
            else if ((packetSize - 6) == sizeof(SystemPacket))
            {
                unsigned int ln = (packetSize - 6) / sizeof(SystemPacket);
                SystemPacket receiveData[ln];

                if ((packetCRC == calcCRC) || (packetCRC == crc16_update(calcCRC, 0xA1)))
                {
                    memcpy(receiveData, &packet[4], packetSize - 6); // Split off data portion of packet (N bytes)
                    if (ln == 1 && receiveData[0].cmd == cmd_ack)
                    {
                        DBG1("ACK Received - CRC Match");
                        if(loraAckState == stInProcess) {
                            loraAckState = stCrcMatch;
                        }
                    }
                    else if (ln == 1 && receiveData[0].cmd == cmd_ping)
                    { // We have received a ping request or reply??
                        if (receiveData[0].param == ping_reply)
                        { // This is a reply to our ping request
                            loraPing.status = stCompleted;
                            DBG1("We have received a ping reply via LoRa from address 0x" + String(sourceMAC, HEX));
                        }
                        else if (receiveData[0].param == ping_request)
                        {
                            DBG1("We have received a ping request from 0x" + String(sourceMAC, HEX) + ", Replying.");
                            pingReplyLoRa(sourceMAC);
                        }
                    }
                    else if (ln == 1 && receiveData[0].cmd == cmd_time && receiveData[0].param > MIN_TS) { // Time received
                        if(timeSource.tmNetIf <= TMIF_LORA) {
                            DBG1("Time rcv from LoRa 0x" + String(sourceMAC, HEX));
                            if(timeSource.tmNetIf == TMIF_NONE) {
                                timeSource.tmNetIf = TMIF_LORA;
                                timeSource.tmAddress = sourceMAC;
                                timeSource.tmSource = TMS_NET;
                                DBG1("Time source is LoRa 0x" + String(sourceMAC, HEX));
                            }
                            if(timeSource.tmAddress == sourceMAC) {
                                if(setTime(receiveData[0].param)) {
                                    timeSource.tmLastTimeSet = millis();
                                }
                            }   
                            else {
                                DBG2("LoRa 0x" + String(sourceMAC, HEX) + " is not time source, discarding request");
                            }
                        }
                    }
                    else if (ln == 1 && receiveData[0].cmd == cmd_time && receiveData[0].param == 0) { // Time requested
                        DBG1("Received LoRa time request from 0x" + String(sourceMAC,HEX));
                        sendTimeLoRa(sourceMAC);
                    }
                    else
                    { // data we have received is not yet programmed.  How we handle is future enhancement.
                        DBG2("Received some LoRa SystemPacket data that is not yet handled.  To be handled in future enhancement.");
                        DBG2("ln: " + String(ln) + "data type: " + String(receiveData[0].cmd));
                    }
                    ackOkLoRaMsg++;
                    return CRC_OK;
                }
                else
                {
                    DBG2("ACK Received CRC Mismatch! Packet CRC is 0x" + String(packetCRC, HEX) + ", Calculated CRC is 0x" + String(calcCRC, HEX));
                    if(loraAckState == stInProcess) {
                        loraAckState = stCrcMismatch;
                    }
                    return CRC_BAD;
                }
            }
        }
        else
        {
            DBG2("Incoming LoRa packet of " + String(packetSize) + " bytes received from address 0x" + String(sourceMAC, HEX) + " destined for node address 0x" + String(destMAC, HEX));
            // printLoraPacket(packet,sizeof(packet));
            return CRC_NULL;
        }
    }
    else
    {
        if (packetSize != 0)
        {
            DBG2("Incoming LoRa packet of " + String(packetSize) + " bytes not processed.");
            //  uint8_t packet[packetSize];
            //  radio.readData((uint8_t *)&packet, packetSize);
            //  printLoraPacket(packet,sizeof(packet));
            return CRC_NULL;
        }
    }
    return CRC_NULL;
}


// Sends packet to any node that is paired to this gateway
void broadcastLoRa()
{
    DBG("Sending to LoRa broadcast buffer");
    transmitLoRaAsync(&loraBroadcast,theData,ln);
}

// Sends packet to neighbor gateways
void sendLoRaNbr(uint8_t interface)
{
  DBG("Sending to LoRa neighbor buffer");
  switch (interface)
  {
    case 1:
      {
        transmitLoRaAsync(&LoRa1,theData,ln);
        break;
      }
    case 2:
      {
        transmitLoRaAsync(&LoRa2,theData,ln);
        break;
      }
  }
}

crcResult LoRaTxRxOperation() 
{
    crcResult crcReturned = CRC_NULL;

    if (operationDone)
    { // the interrupt was triggered
        // DBG("Interrupt triggered");
    // DBG("TxFlag: " + String(transmitFlag));
    // DBG("TxStatus: " + String(TxStatus));

        enableInterrupt = false;
        operationDone = false;
        if (transmitFlag)  // the previous operation was transmission
        {   
            radio.finishTransmit();
            loraTxState = stCompleted;                      
            DBG1("LoRa airtime: " + String(millis() - tx_start_time) + "ms");
            radio.startReceive(); // return to listen mode
            transmitFlag = false;
            // Serial.println("TxINT!");
        }
        else
        { // the previous operation was reception
            crcReturned = receiveLoRa();
            if (!transmitFlag) // return to listen if no transmission was begun
            {
                radio.startReceive();
            }
            // Serial.println("RxINT!");
        }
        delay(10);
        enableInterrupt = true;
    }
    return crcReturned;
}

void handleLoRa()
{
    LoRaTxRxOperation();

    if(loraPing.status == stCompleted) {
        loraPing.response = millis() - loraPing.start;
        DBG1("LoRa Ping Returned: " + String(loraPing.response) + "ms.");
        if(loraPing.address == timeSource.tmAddress) {
            netTimeOffset = loraPing.response/2/1000;
            adjTimeforNetDelay(netTimeOffset);
        }
        loraPing.status = stReady;
        loraPing.start = 0;
        loraPing.timeout = 0;
        loraPing.address = 0;
        loraPing.response = UINT32_MAX;        
    }
    if(loraPing.status == stInProcess && (TDIFF(loraPing.start,loraPing.timeout))) {
        DBG1("No LoRa ping returned within " + String(loraPing.timeout) + "ms.");
        loraPing.status = stReady;
        loraPing.start = 0;
        loraPing.timeout = 0;
        loraPing.address = 0;
        loraPing.response = UINT32_MAX;    
    } 
    
    static int retries = FDRS_LORA_RETRIES;
    static unsigned long loraAckTimeout = 0;
    // Process any DR ACKs in progress
    if(loraTxState == stReady && loraAckState != stReady) {
        if (loraAckState == stCrcMatch)
        {
            DBG1("LoRa ACK Received! CRC OK");
            msgOkLoRa++;
            loraAckState = stReady;
            // reset buffer indexes so that buffer is empty
            endIdxDR = startIdxDR;
            retries = FDRS_LORA_RETRIES;
            if(loraTxState == stCompleted) {
                loraTxState = stReady;
            }
            return; // we're done
        }
        else if(retries == 0) {
            DBG2("Retries Exhausted. Data is lost.");
            loraAckState = stReady;
            // reset buffer indexes so that buffer is empty
            endIdxDR = startIdxDR;
            retries = FDRS_LORA_RETRIES;
            if(loraTxState == stCompleted) {
                loraTxState = stReady;
            }
            return;
        }
        else if (loraAckState == stCrcMismatch)
        {
            DBG1("LoRa ACK Received! CRC BAD");
            //  Resend original packet again if retries are available
            DBG2("Retries: " + String(retries) + " DR Index: " + String(endIdxDR) + " Address: 0x" + String(loraDRBuffTx[endIdxDR].dstAddress,HEX));
            loraAckTimeout = millis();
            loraAckState = stInProcess;
            retries--;
            transmitLoRa(&loraDRBuffTx[endIdxDR].dstAddress, &loraDRBuffTx[endIdxDR].data, (startIdxDR + DRBUFFSIZE - endIdxDR) % DRBUFFSIZE);
        }
        else if (TDIFF(loraAckTimeout,FDRS_ACK_TIMEOUT))
        {
            DBG1("LoRa Timeout waiting for ACK!");
            // resend original packet again if retries are available
            DBG2("Retries: " + String(retries) + " DR Index: " + String(endIdxDR) + " Address: 0x" + String(loraDRBuffTx[endIdxDR].dstAddress,HEX));
            loraAckTimeout = millis();
            loraAckState = stInProcess;
            retries--;
            transmitLoRa(&loraDRBuffTx[endIdxDR].dstAddress, &loraDRBuffTx[endIdxDR].data, (startIdxDR + DRBUFFSIZE - endIdxDR) % DRBUFFSIZE);
        }
        return;
    }

    static unsigned long txWindow = 0;
    // It's polite to Listen more than you talk
    if(TDIFF(txWindow,(TXWINDOWMS + random(0,50)))) {
        // Start Transmit data from the SystemPacket queue
        if(!SPQUEUEEMPTY && (loraTxState == stReady)) {
            DBG2("SP Index: start: " + String(startIdxSP) + " end: " + String(endIdxSP) + " Address: 0x" + String(loraSPBuffTx[endIdxSP].dstAddress,HEX));
            transmitLoRa(&loraSPBuffTx[endIdxSP].dstAddress, &loraSPBuffTx[endIdxSP].data, 1);
            endIdxSP = (endIdxSP + 1) % SPBUFFSIZE;
        }

        // Start Transmit data from the DataReading queue
        if(!DRQUEUEEMPTY && loraTxState == stReady && loraAckState == stReady) 
        {
            if(!ack) 
            {
                DBG2("DR Index: start: " + String(startIdxDR) + " end: " + String(endIdxDR) + " Address: 0x" + String(loraDRBuffTx[endIdxDR].dstAddress,HEX));
                transmitLoRa(&loraDRBuffTx[endIdxDR].dstAddress, &loraDRBuffTx[endIdxDR].data, (startIdxDR + DRBUFFSIZE - endIdxDR) % DRBUFFSIZE);
                endIdxDR = startIdxDR;
            }
            else
            {
                DBG2("Retries: " + String(retries) + " DR Index: " + String(endIdxDR) + " Address: 0x" + String(loraDRBuffTx[endIdxDR].dstAddress,HEX));
                retries--;
                loraAckState = stInProcess;
                loraAckTimeout = millis();
                transmitLoRa(&loraDRBuffTx[endIdxDR].dstAddress, &loraDRBuffTx[endIdxDR].data, (startIdxDR + DRBUFFSIZE - endIdxDR) % DRBUFFSIZE); 
                // Don't reset queue indexes here.  We will reset them in the routine that checks for acks
            }
        }

        // Ping LoRa time master to estimate time delay in radio link
        if(timeSource.tmNetIf == TMIF_LORA) {
            static unsigned long lastTimeSourcePing = 0;

            // ping the time source every 10 minutes
            if(TDIFFMIN(lastTimeSourcePing,10) || lastTimeSourcePing == 0) {
                pingRequestLoRa(timeSource.tmAddress,4000);
                lastTimeSourcePing = millis();
            }
        }
        txWindow = millis();
    }
    
    // Change to ready at the end so only one transmit happens per function call
    if(loraTxState == stCompleted) {
        loraTxState = stReady;
    }
    return;
}

// Only for use in nodes - not intended to be used in gateway
bool reqTimeLoRa() {
    SystemPacket sys_packet = {.cmd = cmd_time, .param = 0};

    DBG1("Requesting time from gateway 0x" + String(gtwyAddress,HEX));
    if(loraTxState == stReady) {
        transmitLoRa(&gtwyAddress,&sys_packet,1);
        return true;
    }
    else {
        if(transmitLoRaAsync(&gtwyAddress, &sys_packet, 1))
        {
            return true;
        }
    }
    return false;
}