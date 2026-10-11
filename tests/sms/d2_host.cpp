#include "../../src/communication/d2_codec.h"
#include "../../src/communication/d2_store.h"
#include "../../src/communication/gsm.h"
#include "../../src/communication/sms_scheduler.h"
#include "../../src/telemetry/energy_window.h"
#include "../../src/communication/gsm_startup.h"
#include "host_crypto.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <map>
#include <string>
#include <vector>
using namespace djua;

static uint8_t testKey[32];
struct MemoryStore : D2Store {
    std::map<std::string, std::vector<uint8_t>> records;
    int writes = 0, failWrite = -1;
    ReadResult read(const char* name, void* out, size_t size) override {
        const auto found = records.find(name);
        if (found == records.end()) return ReadResult::Missing;
        if (found->second.size() != size) return ReadResult::Error;
        memcpy(out, found->second.data(), size); return ReadResult::Ok;
    }
    bool write(const char* name, const void* data, size_t size) override {
        if (++writes == failWrite) return false;
        const auto* bytes = static_cast<const uint8_t*>(data);
        records[name] = {bytes, bytes + size}; return true;
    }
};
struct FakeUart : GsmIo {
    std::deque<char> rx;
    std::string tx;
    bool failWrites = false;
    int available() override { return static_cast<int>(rx.size()); }
    int read() override { if (rx.empty()) return -1; const char c = rx.front(); rx.pop_front(); return c; }
    bool write(const char* p, size_t n) override { if (failWrites) return false; tx.append(p, n); return true; }
    void feed(const std::string& data) { for (char c : data) rx.push_back(c); }
};
static D2Frame event(bool exit = true) {
    D2Frame frame; frame.event = true; frame.exit = exit; return frame;
}
static void provision(D2Journal& journal) {
    journal.begin(); assert(journal.state() == D2State::Unprovisioned);
    assert(journal.provision("TEST-001", testKey, "+243810000001", "bench-2026"));
}
static void ready(GsmEngine& gsm, FakeUart& io, uint32_t now = 0) {
    gsm.begin(true, now); gsm.update(now);
    for (int i = 0; i < 11; ++i) {
        io.feed(i == 3 ? "+CPIN: READY\r\nOK\r\n" :
                i == 4 ? "+CEREG: 0,1\r\nOK\r\n" :
                i == 5 ? "+CREG: 0,1\r\nOK\r\n" :
                i == 6 ? "+CSMS: 0,1,1,1\r\nOK\r\n" :
                i == 7 ? "+CSCA: \"+243810000000\",145\r\nOK\r\n" : "OK\r\n");
        gsm.update(now);
    }
    assert(gsm.ready() && gsm.registered());
}
static void energyTests() {
    EnergyWindow window;
    window.sample(0, 360, true); window.sample(10000, 720, true);
    assert(window.complete(10000) && fabs(window.wattHours() - 1.5) < 1e-9);
    window.reset(10000, 720, true); window.sample(20000, -720, true);
    assert(window.complete(20000) && fabs(window.wattHours()) < 1e-9);
    window.sample(30000, -720, true); assert(fabs(window.wattHours() + 2) < 1e-9);
    window.sample(50001, -720, true); assert(!window.complete(50001));
    window.sample(60001, -720, true); assert(!window.complete(60001));
    window.reset(60001, -720, true); assert(window.complete(60001));
    window.sample(70001, 0, false); assert(!window.complete(70001));
    window.reset(70001, 0, false); window.sample(80001, 1, true); assert(!window.complete(80001));
    window.reset(UINT32_MAX - 4999, 360, true); window.sample(5000, 360, true);
    assert(window.complete(5000) && fabs(window.wattHours() - 1) < 1e-9);
    assert(!window.complete(25001));
    window.reset(0, 10, true); window.sample(0, 10, true); assert(!window.complete(0));
    window.reset(0, 1, true); window.sample(10000, NAN, true); assert(!window.complete(10000));
}
static void codecEdgeTests() {
    D2Frame frame = event(); strcpy(frame.device, "A"); frame.sequence = 1;
    char text[D2_SMS_CAPACITY];
    assert(encodeD2(frame, nullptr, true, hostHmac, text) == D2Error::AuthRequired && !*text);
    assert(encodeD2(frame, nullptr, false, hostHmac, text) == D2Error::Ok);
    frame.flags = 6; frame.values[Lat] = {-0.000005, true}; frame.values[Lon] = {0.000005, true};
    frame.values[Distance] = {0.5, true};
    assert(encodeD2(frame, testKey, true, hostHmac, text) == D2Error::Ok);
    assert(strstr(text, ",GX,-1,1,1,06,"));
    D2Frame parsed; assert(decodeD2(text, testKey, true, hostHmac, parsed) == D2Error::Ok);
    frame.values[Lat].value = 90.000001;
    assert(encodeD2(frame, testKey, true, hostHmac, text) == D2Error::OutOfRange && !*text);
    frame.values[Lat].value = NAN; assert(encodeD2(frame, testKey, true, hostHmac, text) == D2Error::OutOfRange);
    frame.values[Lat].value = 0; frame.values[Lon].value = 0; frame.values[Distance].value = 0;
    assert(encodeD2(frame, testKey, true, hostHmac, text) == D2Error::Ok && strstr(text, ",GX,0,0,0,06,"));
    frame.flags = 4; assert(encodeD2(frame, testKey, true, hostHmac, text) == D2Error::FlagValueMismatch);
    size_t count = 0; assert(countD2Septets("D2E,A,^", count) == D2Error::NonGsm7);
    assert(countD2Septets(std::string(161, 'A').c_str(), count) == D2Error::SmsTooLong);
    assert(validE164("+243810000001") && !validE164("243810000001") && !validE164("+01234"));
    assert(!validDeviceId("lower") && !validDeviceId(std::string(33, 'A').c_str()));
    char unterminatedId[33]; memset(unterminatedId, 'A', sizeof(unterminatedId));
    char unterminatedPhone[17]; memset(unterminatedPhone, '1', sizeof(unterminatedPhone)); unterminatedPhone[0] = '+';
    assert(!validDeviceId(unterminatedId) && !validE164(unterminatedPhone));
    uint32_t epoch = 0;
    assert(d2UtcEpoch(2026, 9, 27, 16, 30, 0, 60, epoch) && epoch == 1790523000);
    assert(d2UtcEpoch(2100, 1, 1, 0, 59, 59, 60, epoch) && epoch == 4102444799UL);
    assert(!d2UtcEpoch(2000, 1, 1, 0, 0, 0, 60, epoch));
    assert(!d2UtcEpoch(2026, 2, 29, 12, 0, 0, 60, epoch));
    assert(d2UtcEpoch(2024, 2, 29, 12, 0, 0, 60, epoch));
    assert(!d2UtcEpoch(2026, 1, 1, 12, 0, 0, 0, epoch));
    // AUTH base64url peut contenir '_' meme si les premiers vecteurs publics
    // n'en contiennent pas : le contenu signe ne doit jamais etre normalise.
    D2Frame underscore = event(); strcpy(underscore.device, "A"); bool found = false;
    for (uint32_t sequence = 1; sequence < 128 && !found; ++sequence) {
        underscore.sequence = sequence;
        assert(encodeD2(underscore, testKey, true, hostHmac, text) == D2Error::Ok);
        found = strchr(text, '_') != nullptr;
    }
    assert(found && decodeD2(text, testKey, true, hostHmac, parsed) == D2Error::Ok);
    text[strlen(text) - 1] = text[strlen(text) - 1] == 'A' ? 'B' : 'A';
    assert(decodeD2(text, testKey, false, hostHmac, parsed) == D2Error::AuthInvalid); // meme en dev
}
static void d2t2EdgeTests() {
    D2Frame frame; strcpy(frame.device, "A"); frame.sequence = 1; frame.interval = 10;
    frame.flags = 384;
    frame.values[DcV] = {0, true}; frame.values[DcI] = {-0.0005, true};
    frame.values[DcP] = {-0.05, true}; frame.values[DcE] = {-0.005, true};
    char text[D2_SMS_CAPACITY]; D2Frame decoded;
    assert(encodeD2T2(frame, testKey, true, hostHmac, text) == D2Error::Ok);
    assert(decodeD2(text, testKey, true, hostHmac, decoded) == D2Error::Ok);
    assert(decoded.flags == 384 && decoded.values[DcV].present && decoded.values[DcV].value == 0);
    assert(decoded.values[DcI].value == -0.001 && decoded.values[DcP].value == -0.1 && decoded.values[DcE].value == -0.01);
    frame.values[DcI].value = 0.0005; frame.values[DcP].value = 0.05; frame.values[DcE].value = 0.005;
    assert(encodeD2T2(frame, testKey, true, hostHmac, text) == D2Error::Ok);
    assert(decodeD2(text, testKey, true, hostHmac, decoded) == D2Error::Ok);
    assert(decoded.values[DcI].value == 0.001 && decoded.values[DcP].value == 0.1 && decoded.values[DcE].value == 0.01);
    frame.values[DcV].value = 100.0001;
    assert(encodeD2T2(frame, testKey, true, hostHmac, text) == D2Error::OutOfRange && !*text);
    frame.values[DcV].value = NAN;
    assert(encodeD2T2(frame, testKey, true, hostHmac, text) == D2Error::OutOfRange);
    frame.flags = 256; assert(encodeD2T2(frame, testKey, true, hostHmac, text) == D2Error::FlagDependency);
    frame.flags = 512; assert(encodeD2T2(frame, testKey, true, hostHmac, text) == D2Error::InvalidFlags);

    // A legacy SMS already persisted keeps its exact bytes after upgrading.
    MemoryStore memory; D2Journal journal(memory, hostHmac); provision(journal);
    D2Frame legacy; strcpy(legacy.device, "TEST-001"); legacy.interval = 1800;
    assert(journal.nextSequence(legacy.sequence));
    D2Queue queue; queue.count = 1; queue.items[0].sequence = legacy.sequence;
    assert(encodeD2(legacy, testKey, true, hostHmac, queue.items[0].text) == D2Error::Ok);
    const std::string original = queue.items[0].text;
    queue.checksum = d2Checksum(&queue, offsetof(D2Queue, checksum));
    assert(memory.write("queue", &queue, sizeof(queue)));
    D2Journal upgraded(memory, hostHmac); upgraded.begin();
    assert(upgraded.state() == D2State::Ready && original == upgraded.at(0)->text);
    D2Error error; assert(upgraded.enqueue(legacy, error) == EnqueueResult::Stored);
    assert(!strncmp(upgraded.at(1)->text, "D2T2,", 5) && upgraded.at(1)->sequence == 257);
    assert(upgraded.enqueue(event(), error) == EnqueueResult::Stored && upgraded.at(2)->sequence == 258);
    assert(upgraded.recordAttempt(1)); const std::string pending = upgraded.at(1)->text;
    D2Journal reboot(memory, hostHmac); reboot.begin();
    assert(reboot.state() == D2State::Ready && pending == reboot.at(1)->text && original == reboot.at(0)->text);
}
static void startupTests() {
    GsmStartup start; start.begin(true, UINT32_MAX - 500);
    start.update(498); assert(!start.asserted() && !start.ready());
    start.update(499); assert(start.asserted());
    start.update(1698); assert(start.asserted());
    start.update(1699); assert(!start.asserted() && !start.ready());
    start.update(6699); assert(start.ready());
    start.begin(false, 0); start.update(4999); assert(!start.ready() && !start.asserted());
    start.update(5000); assert(start.ready());
}
static void journalTests() {
    MemoryStore memory; D2Journal journal(memory, hostHmac); provision(journal);
    uint32_t seq = 0;
    assert(journal.nextSequence(seq) && seq == 1);
    D2Journal afterReboot(memory, hostHmac); afterReboot.begin();
    assert(afterReboot.nextSequence(seq) && seq == 257);
    assert(!afterReboot.provision("TEST-002", testKey, "+243810000001", "replace"));
    memory.records.erase("sequence"); D2Journal broken(memory, hostHmac); broken.begin();
    assert(broken.state() == D2State::CounterFault && !broken.nextSequence(seq));

    MemoryStore endMemory; D2Journal last(endMemory, hostHmac); last.begin();
    assert(last.provision("LAST", testKey, "+243810000001", "limit", UINT32_MAX));
    assert(last.nextSequence(seq) && seq == UINT32_MAX);
    assert(!last.nextSequence(seq) && last.state() == D2State::SequenceExhausted);
    D2Journal exhausted(endMemory, hostHmac); exhausted.begin(); assert(exhausted.state() == D2State::SequenceExhausted);

    MemoryStore failed; D2Journal bad(failed, hostHmac); provision(bad);
    failed.failWrite = failed.writes + 1;
    assert(!bad.nextSequence(seq) && bad.state() == D2State::CounterFault);
    failed.failWrite = -1; D2Journal recovered(failed, hostHmac); recovered.begin();
    assert(recovered.nextSequence(seq) && seq == 1); // aucune sequence n'avait ete remise
    for (int interrupted = 1; interrupted <= 3; ++interrupted) {
        MemoryStore torn; torn.failWrite = interrupted; D2Journal p(torn, hostHmac); p.begin();
        assert(!p.provision("TORN", testKey, "+243810000001", "test"));
        assert(p.state() == D2State::StorageFault);
        D2Journal restart(torn, hostHmac); restart.begin();
        assert(restart.state() != D2State::Ready);
    }
    MemoryStore virgin; D2Journal unprovisioned(virgin, hostHmac); unprovisioned.begin();
    D2Error error;
    assert(unprovisioned.enqueue(event(), error) == EnqueueResult::NotReady && virgin.records.empty());
}
static void queueTests() {
    MemoryStore memory; D2Journal journal(memory, hostHmac); provision(journal);
    D2Error error;
    D2Frame telemetry; telemetry.interval = 1800;
    for (size_t i = 0; i < D2_PERIODIC_CAPACITY; ++i) assert(journal.enqueue(telemetry, error) == EnqueueResult::Stored);
    assert(!strncmp(journal.at(0)->text, "D2T2,", 5));
    assert(journal.enqueue(telemetry, error) == EnqueueResult::Full);
    for (size_t i = D2_PERIODIC_CAPACITY; i < D2_QUEUE_CAPACITY; ++i)
        assert(journal.enqueue(event(i % 2 != 0), error) == EnqueueResult::Stored);
    assert(journal.enqueue(event(), error) == EnqueueResult::Full);
    const std::string original = journal.at(4)->text;
    assert(journal.recordAttempt(4)); assert(original == journal.at(4)->text);
    D2Journal restored(memory, hostHmac); restored.begin();
    assert(restored.state() == D2State::Ready && restored.count() == 16);
    assert(original == restored.at(4)->text && restored.at(4)->attempts == 1);
    assert(restored.acceptedByModem(4) && restored.count() == 15);
    D2Journal twice(memory, hostHmac); twice.begin(); assert(twice.count() == 15);
    memory.records["queue"][8] ^= 1; D2Journal corrupt(memory, hostHmac); corrupt.begin();
    assert(corrupt.state() == D2State::StorageFault);

    MemoryStore loss; D2Journal j(loss, hostHmac); provision(j);
    loss.failWrite = loss.writes + 2; // reservation reussie, persistance SMS echouee
    assert(j.enqueue(event(), error) == EnqueueResult::StorageError && j.count() == 0);
    loss.failWrite = -1; D2Journal replay(loss, hostHmac); replay.begin(); uint32_t sequence;
    assert(replay.nextSequence(sequence) && sequence == 257);
    // CMGS accepte mais commit de retrait echoue : meme SMS apres reboot.
    MemoryStore uncertain; D2Journal sender(uncertain, hostHmac); provision(sender);
    assert(sender.enqueue(event(), error) == EnqueueResult::Stored);
    const std::string pending = sender.at(0)->text;
    uncertain.failWrite = uncertain.writes + 1;
    assert(!sender.acceptedByModem(0) && sender.state() == D2State::StorageFault);
    uncertain.failWrite = -1;
    D2Journal reboot(uncertain, hostHmac); reboot.begin();
    assert(reboot.state() == D2State::Ready && reboot.count() == 1 && pending == reboot.at(0)->text);
}
static void modemTests() {
    FakeUart io; GsmEngine gsm(io); gsm.begin(false, 0); gsm.update(999999); assert(io.tx.empty());
    ready(gsm, io); assert(io.tx.find("AT+CSCS=\"IRA\"") != std::string::npos);
    assert(gsm.submit("+243810000001", "D2E,A,1,-,0,GX,-,-,-,00,-", 10));
    io.feed("\r\n> "); gsm.update(11); assert(gsm.busy() && io.tx.back() == 26);
    io.feed("\r\n+CMGS: 42\r\nOK\r\n"); gsm.update(12);
    assert(gsm.takeResult() == GsmResult::AcceptedByModem && gsm.ready());
    assert(gsm.submit("+243810000001", "D2E,A,1,-,0,GX,-,-,-,00,-", 20));
    io.feed(">\r\n+CMS ERROR: 500\r\n"); gsm.update(21); assert(gsm.takeResult() == GsmResult::Failed);
    ready(gsm, io, UINT32_MAX - 1000);
    assert(gsm.submit("+243810000001", "D2E,A,1,-,0,GX,-,-,-,00,-", UINT32_MAX - 1000));
    gsm.update(9000); assert(gsm.takeResult() == GsmResult::Failed); // timeout au wrap
    ready(gsm, io);
    assert(gsm.submit("+243810000001", "D2E,A,1,-,0,GX,-,-,-,00,-", 1));
    io.feed(">\r\nOK\r\n"); gsm.update(2); assert(gsm.takeResult() == GsmResult::Failed); // OK seul insuffisant
    ready(gsm, io); assert(gsm.submit("+243810000001", "D2E,A,1,-,0,GX,-,-,-,00,-", 1));
    io.feed(">"); gsm.update(2); gsm.update(120002); assert(gsm.takeResult() == GsmResult::Failed);
    FakeUart noSim; GsmEngine locked(noSim); locked.begin(true, 0); locked.update(0);
    for (int i = 0; i < 4; ++i) { noSim.feed(i == 3 ? "+CPIN: SIM PIN\r\nOK\r\n" : "OK\r\n"); locked.update(0); }
    assert(!locked.ready() && !locked.registered());
    FakeUart noNetwork; GsmEngine searching(noNetwork); searching.begin(true, 0); searching.update(0);
    for (int i = 0; i < 6; ++i) {
        noNetwork.feed(i == 3 ? "+CPIN: READY\r\nOK\r\n" : i == 4 ? "+CEREG: 0,2\r\nOK\r\n" : i == 5 ? "+CREG: 0,2\r\nOK\r\n" : "OK\r\n");
        searching.update(0);
    }
    assert(!searching.ready());
    ready(gsm, io); io.tx.clear();
    assert(gsm.submit("+243810000001", "D2E,A,1,-,0,GX,-,-,-,00,TEST_TAG_00", 1));
    io.feed(">"); gsm.update(2);
    assert(io.tx.find("TEST_TAG_00") != std::string::npos); // '_' ASCII via IRA
    io.feed("\r\n+CMGS: 1\r\nOK\r\n"); gsm.update(3); gsm.takeResult();
    io.feed("+CREG: 5,\"0001\",\"0002\"\r\n"); gsm.update(4); assert(gsm.registered());
    io.feed("+CEREG: 0\r\n+CREG: 0\r\n"); gsm.update(5); assert(!gsm.ready());
    io.feed("+CEREG: 0,5\r\nOK\r\n+CREG: 0,5\r\nOK\r\n+CSMS: 0,1,1,1\r\nOK\r\n+CSCA: \"+243810000000\",145\r\nOK\r\n"); gsm.update(6); assert(gsm.ready());
    io.failWrites = true;
    assert(gsm.submit("+243810000001", "D2E,A,1,-,0,GX,-,-,-,00,-", 7));
    assert(gsm.takeResult() == GsmResult::Failed && gsm.fault() == GsmFault::WriteError);
}
static void schedulerTests() {
    MemoryStore memory; D2Journal journal(memory, hostHmac); provision(journal); D2Error error;
    D2Frame telemetry; telemetry.interval = 1800;
    assert(journal.enqueue(telemetry, error) == EnqueueResult::Stored);
    assert(journal.enqueue(event(), error) == EnqueueResult::Stored);
    const std::string urgent = journal.at(1)->text, periodic = journal.at(0)->text;
    FakeUart io; GsmEngine gsm(io); ready(gsm, io); SmsScheduler scheduler(journal, gsm);
    scheduler.update(1); io.feed(">"); scheduler.update(2);
    assert(io.tx.find(urgent) != std::string::npos && io.tx.find(periodic) == std::string::npos);
    io.feed("\r\n+CMS ERROR: 500\r\n"); scheduler.update(3);
    assert(scheduler.takeResult() == GsmResult::Failed && journal.count() == 2);
    assert(journal.at(1)->text == urgent);
    // Coupure apres CMGS incertain : les memes octets survivent, sans nouvel id.
    D2Journal resumed(memory, hostHmac); resumed.begin(); assert(resumed.at(1)->text == urgent);
    FakeUart nextIo; GsmEngine nextGsm(nextIo); ready(nextGsm, nextIo); SmsScheduler next(resumed, nextGsm);
    next.update(0); nextIo.feed(">\r\n+CMGS: 7\r\nOK\r\n"); next.update(1);
    assert(next.takeResult() == GsmResult::AcceptedByModem && resumed.count() == 1);
    assert(nextIo.tx.find(urgent) != std::string::npos);
    // The periodic D2T2 is next; a submit timeout must preserve it byte for byte.
    nextIo.feed(">"); next.update(2);
    assert(nextIo.tx.find(periodic) != std::string::npos);
    next.update(120002); assert(next.takeResult() == GsmResult::Failed);
    D2Journal retryPeriodic(memory, hostHmac); retryPeriodic.begin();
    assert(retryPeriodic.count() == 1 && periodic == retryPeriodic.at(0)->text);
    assert(SmsScheduler::retryDelay(1) == 30000 && SmsScheduler::retryDelay(UINT32_MAX) == 900000);
    MemoryStore backoffStore; D2Journal backoffJournal(backoffStore, hostHmac); provision(backoffJournal);
    assert(backoffJournal.enqueue(event(), error) == EnqueueResult::Stored);
    FakeUart backoffIo; GsmEngine backoffGsm(backoffIo); ready(backoffGsm, backoffIo, 100);
    SmsScheduler backoff(backoffJournal, backoffGsm, 1000, 4000);
    backoff.update(100); backoffIo.feed(">\r\n+CMS ERROR: 500\r\n"); backoff.update(101);
    ready(backoffGsm, backoffIo, 1100); backoff.update(1100);
    assert(backoffJournal.at(0)->attempts == 1); // pas encore echeance
    backoff.update(1101); assert(backoffJournal.at(0)->attempts == 2);
    backoffIo.feed(">\r\n+CMGS: 8\r\nOK\r\n"); backoff.update(1102);
    assert(backoffJournal.count() == 0);
}
int main(int argc, char** argv) {
    for (int i = 0; i < 32; ++i) testKey[i] = static_cast<uint8_t>(i);
    if (argc >= 4 && std::string(argv[1]) == "check") {
        D2Frame frame; std::cout << d2ErrorName(decodeD2(argv[2], testKey, std::string(argv[3]) == "production", hostHmac, frame));
        return 0;
    }
    if ((argc == 25 || argc == 29) && (std::string(argv[1]) == "encode" || std::string(argv[1]) == "pack")) {
        D2Frame frame; frame.event = std::string(argv[2]) == "D2E";
        if (strlen(argv[3]) > 32) return 2; strcpy(frame.device, argv[3]);
        frame.sequence = static_cast<uint32_t>(strtoull(argv[4], nullptr, 10));
        frame.rtc = std::string(argv[5]) == "-" ? 0 : static_cast<uint32_t>(strtoull(argv[5], nullptr, 10));
        frame.uptime = static_cast<uint32_t>(strtoull(argv[6], nullptr, 10));
        if (frame.event) frame.exit = std::string(argv[7]) == "GX";
        else frame.interval = static_cast<uint32_t>(strtoul(argv[7], nullptr, 10));
        frame.flags = static_cast<uint16_t>(strtoul(argv[8], nullptr, 10));
        const size_t n = argc == 29 ? FieldCount : D2_LEGACY_FIELD_COUNT;
        for (size_t i = 0; i < n; ++i)
            if (std::string(argv[9 + i]) != "-") frame.values[i] = {strtod(argv[9 + i], nullptr), true};
        const bool production = std::string(argv[argc - 1]) != "development";
        const auto encoder = std::string(argv[2]) == "D2T2" ? encodeD2T2 : encodeD2;
        if (std::string(argv[1]) == "pack") {
            uint8_t raw[D2T2_PAYLOAD_BYTES];
            if (packD2T2(frame, raw) != D2Error::Ok) return 1;
            for (uint8_t byte : raw) { char hex[3]; snprintf(hex, sizeof(hex), "%02x", byte); std::cout << hex; }
            return 0;
        }
        char text[D2_SMS_CAPACITY]; const auto error = encoder(frame,
            production ? testKey : nullptr, production, hostHmac, text);
        if (error != D2Error::Ok) { std::cout << d2ErrorName(error); return 1; }
        std::cout << text; return 0;
    }
    energyTests(); codecEdgeTests(); d2t2EdgeTests(); startupTests(); journalTests(); queueTests(); modemTests(); schedulerTests();
    std::cout << "8 groupes C++ PASS : D2/UTC, D2T2/migration, energie, compteur, file, modem, scheduler, PWRKEY\n";
}
