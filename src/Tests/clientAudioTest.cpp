#include "Network/client.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

struct ClientAudioTest{
    static void Check(bool condition, const char* message){
        if(!condition) throw std::runtime_error(message);
    }
    static void Run(){
        auto mutes = std::make_shared<MuteState>();
        Client client("127.0.0.1", 8080, "test", Json::object(), mutes, [](const Json&){}, false);
        mutes->Set({{"microphone", false}, {"all", false}, {"team", false}});
        client.groupId = 1; client.allChannel = 100; client.teamChannel = 101;
        client.clientId = 99; client.serverInstance = 42; client.receiveRevision = 5;
        client.session = {{"team", 1}, {"mutes", mutes->Snapshot()}};
        client.activeClients = {{1, "first"}, {2, "second"}};
        client.peerRevisions = {{1, 3}, {2, 3}};
        client.peerTeams = {{1, 1}, {2, 1}};
        std::array<float,960> input{};
        for(unsigned i = 0; i < input.size(); i++) input[i] = .1f * std::sin(i * .08f);
        OpusEncoderWrapper encoder;
        Check(encoder.Initialize(48000, 1, 32000), "Encoder initialization");
        std::array<std::uint8_t,4000> encoded{};
        const int size = encoder.Encode(input.data(), 960, encoded.data(), 4000);
        Check(size > 0, "Encoding failed");
        std::vector<std::uint8_t> payload(encoded.begin(), encoded.begin() + size);
        RoutedPacket packet(PacketType::Voice, payload);
        packet.SetSequenceNumber(1);
        packet.SetContext(42, 1, 100, 3, 5);
        for(unsigned id : {1u, 2u}){
            packet.SetClientId(id);
            client.networkQueue.Push(packet.Serialize());
        }
        client.ProcessReceivedVoice();
        Check(client.remoteClients.size() == 2, "Speakers were not independently buffered");
        const auto now = Client::Clock::now();
        for(auto& [id, remote] : client.remoteClients) remote.playbackStart = now;
        client.ProcessPlayback(now, 0);
        Check(client.decodedFrames == 2 && client.mixedFrames == 1, "Simultaneous speakers did not mix into one frame");
        std::array<float,960> mixed{}, reference{};
        Check(client.audioPlayback.pcmBuffer.PopExact(mixed.data(), mixed.size()), "Mixed output size");
        OpusDecoderWrapper decoder;
        Check(decoder.Initialize(48000, 1), "Decoder initialization");
        Check(decoder.Decode(payload.data(), size, reference.data(), 960) == 960, "Reference decode");
        float peak = 1;
        for(float value : reference) peak = std::max(peak, std::abs(value * 2));
        for(unsigned i = 0; i < mixed.size(); i++){
            Check(std::abs(mixed[i] - reference[i] * 2 / peak) < .00001f, "Mixed samples changed unexpectedly");
        }
        client.ClearAudio();
        client.receiveRevision = 6;
        client.networkQueue.Push(packet.Serialize());
        client.ProcessReceivedVoice();
        Check(client.remoteClients.empty(), "Old recipient revision accepted");
        client.receiveRevision = 5;
        mutes->all = true;
        client.networkQueue.Push(packet.Serialize());
        client.ProcessReceivedVoice();
        Check(client.remoteClients.empty(), "Incoming All mute failed");
        packet.SetContext(42, 1, 101, 3, 5);
        client.networkQueue.Push(packet.Serialize());
        client.ProcessReceivedVoice();
        Check(client.remoteClients.size() == 1, "All mute incorrectly blocked Team");
        client.ClearAudio();
        mutes->team = true;
        client.networkQueue.Push(packet.Serialize());
        client.ProcessReceivedVoice();
        Check(client.remoteClients.empty(), "Incoming Team mute failed");
        mutes->microphone = true;
        client.HandleCapture(input.data(), input.size());
        Check(client.captureQueue.Size() == 0, "Muted capture was queued");
        mutes->microphone = false;
        client.HandleCapture(input.data(), input.size());
        Check(client.captureQueue.Size() == 1, "Unmuted capture was not queued");
        const auto generation = mutes->captureGeneration.load();
        mutes->Set({{"microphone", true}, {"all", true}, {"team", true}});
        Check(mutes->captureGeneration > generation, "Mute did not invalidate captured audio");
        client.ClearAudio();
    }
};

int main(){
    try{ ClientAudioTest::Run(); std::cout << "Client audio tests passed\n"; }
    catch(const std::exception& error){ std::cerr << error.what() << '\n'; return 1; }
}

