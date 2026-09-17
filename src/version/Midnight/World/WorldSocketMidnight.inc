// Midnight-only extension of WorldSocket.
// Included inside class WorldSocket when AE_HAS_WORLD_V2_PROFILE is enabled.

public:
    uint32_t getMidnightRealmId() const { return m_battleNetRealmId; }
    uint32_t getMidnightRegionId() const { return m_battleNetRegionId; }
    bool sendMidnightPacket(AscEmu::Version::Midnight::Packets::Packet& packet);

protected:
    bool sendBattleNetV2ServerInitializer();
    bool processBattleNetV2Initializer();
    bool sendBattleNetV2AuthChallenge();
    bool sendBattleNetV2Packet(uint32_t opcode, const uint8_t* payload, uint32_t payloadSize);
    bool processBattleNetV2Packet();
    bool processBattleNetV2AuthSession(uint32_t opcode, const std::vector<uint8_t>& payload);
    bool sendBattleNetV2EnterEncryptedMode(uint32_t regionGroup);
    bool processBattleNetV2EnterEncryptedModeAck(uint32_t opcode, const std::vector<uint8_t>& payload);
    bool finalizeBattleNetV2WorldSession();
    bool sendBattleNetV2AuthResponse();
    bool processBattleNetV2EncryptedPacket();
    // Midnight opcode handlers. These intentionally mirror the legacy WorldSession
    // handler style while keeping the modern transport on WorldSocket.
    bool handleMidnightCharEnumOpcode(AscEmu::Version::Midnight::Packets::Packet& packet);
    bool handleMidnightCheckCharacterNameOpcode(AscEmu::Version::Midnight::Packets::Packet& packet);
    bool handleMidnightCharCreateOpcode(AscEmu::Version::Midnight::Packets::Packet& packet);
    bool handleMidnightUndeleteCooldownOpcode(AscEmu::Version::Midnight::Packets::Packet& packet);
    bool handleMidnightDbQueryBulkOpcode(AscEmu::Version::Midnight::Packets::Packet& packet);
    bool handleMidnightPingOpcode(AscEmu::Version::Midnight::Packets::Packet& packet);
    bool handleMidnightSocialContractOpcode(AscEmu::Version::Midnight::Packets::Packet& packet);
    bool handleMidnightServerTimeOffsetOpcode(AscEmu::Version::Midnight::Packets::Packet& packet);
    bool encryptBattleNetV2Payload(std::vector<uint8_t>& data, std::array<uint8_t, 12>& tag);
    bool decryptBattleNetV2Payload(std::vector<uint8_t>& data, const std::array<uint8_t, 12>& tag);

private:
    friend class AscEmu::Version::Midnight::OpcodeHandlerRegistry;

    enum class BattleNetV2State : uint8_t
    {
        Disabled,
        AwaitClientInitializer,
        AwaitAuthSession,
        AuthSessionObserved,
        AuthDigestVerified,
        AwaitEncryptionAck,
        Encrypted
    };

    BattleNetV2State m_battleNetV2State{BattleNetV2State::Disabled};
    std::array<uint8_t, 32> m_battleNetServerChallenge{};
    std::array<uint8_t, 32> m_battleNetDosChallenge{};
    std::array<uint8_t, 40> m_battleNetSessionKey{};
    std::array<uint8_t, 32> m_battleNetEncryptionKey{};
    uint32_t m_battleNetClientBuild{0};
    uint32_t m_battleNetAccountId{0};
    uint32_t m_battleNetGameAccountId{0};
    uint32_t m_battleNetRealmId{0};
    uint32_t m_battleNetRegionId{0};
    std::string m_battleNetGameAccountName;
    uint64_t m_battleNetV2SendCounter{0};
    uint64_t m_battleNetV2RecvCounter{0};
    uint32_t m_battleNetV2PacketOpcode{0};
    uint32_t m_battleNetV2PacketRemaining{0};
    bool m_battleNetV2EncryptedHeaderReady{false};
    uint32_t m_battleNetV2EncryptedPacketSize{0};
    std::array<uint8_t, 12> m_battleNetV2EncryptedPacketTag{};
    std::array<uint8_t, 4> m_battleNetV2EncryptedOpcode{};
    uint64_t m_battleNetV2CryptoSendCounter{0};
    uint64_t m_battleNetV2CryptoRecvCounter{0};
