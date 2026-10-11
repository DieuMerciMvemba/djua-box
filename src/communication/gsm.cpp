#include "gsm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace djua {
namespace {
constexpr const char* commands[] = {
    "AT\r", "ATE0\r", "AT+CMEE=2\r", "AT+CPIN?\r", "AT+CREG?\r",
    // IRA = ASCII cote UART ; DCS=0 impose GSM-7 cote SMS. Important pour '_',
    // dont le code ASCII 0x5F differe du code GSM-7 0x11.
    "AT+CMGF=1\r", "AT+CSCS=\"IRA\"\r", "AT+CSMP=17,167,0,0\r"
};
}
void GsmEngine::begin(bool configured, uint32_t now) {
    state_ = configured ? GsmState::Backoff : GsmState::Disabled;
    since_ = now - retryInterval_; result_ = GsmResult::None; fault_ = GsmFault::None;
    registered_ = simReady_ = polling_ = false; used_ = 0;
}
void GsmEngine::startCommand(uint32_t now) {
    state_ = GsmState::Initializing; since_ = now; used_ = 0;
    if (!io_.write(commands[step_], strlen(commands[step_]))) fail(now, GsmFault::WriteError);
}
void GsmEngine::fail(uint32_t now, GsmFault fault) {
    fault_ = fault;
    // ESC abandonne une saisie de SMS eventuellement encore ouverte.
    const char escape = 27; io_.write(&escape, 1);
    if (busy()) result_ = GsmResult::Failed;
    state_ = GsmState::Backoff; since_ = now; used_ = 0;
    registered_ = simReady_ = polling_ = false;
}
void GsmEngine::line(uint32_t now) {
    line_[used_] = 0; used_ = 0;
    if (!strcmp(line_, "+CPIN: READY")) simReady_ = true;
    if (!strncmp(line_, "+CREG:", 6)) {
        char* end = nullptr;
        int registration = static_cast<int>(strtol(line_ + 6, &end, 10));
        if (end && *end == ',') {
            ++end; while (*end == ' ') ++end;
            // Query : <n>,<stat> ; URC detaillee : <stat>,"LAC",... .
            if (*end >= '0' && *end <= '5') registration = static_cast<int>(strtol(end, nullptr, 10));
        }
        registered_ = registration == 1 || registration == 5;
    }
    if (!strncmp(line_, "+CMGS:", 6) && state_ == GsmState::Submitting) {
        const char* p = line_ + 6; while (*p == ' ') ++p;
        sawCmgs_ = *p >= '0' && *p <= '9';
    }
    if (!strcmp(line_, "ERROR") || !strncmp(line_, "+CMS ERROR", 10) ||
        !strncmp(line_, "+CME ERROR", 10) || !strcmp(line_, "NORMAL POWER DOWN")) {
        fail(now, GsmFault::CommandError); return;
    }
    if (strcmp(line_, "OK")) return;
    if (state_ == GsmState::Submitting) {
        if (!sawCmgs_) { fail(now, GsmFault::UnexpectedOk); return; }
        fault_ = GsmFault::None;
        result_ = GsmResult::AcceptedByModem; state_ = GsmState::Ready;
        lastCheck_ = now; return;
    }
    if (state_ != GsmState::Initializing) return;
    if (step_ == 3 && !simReady_) { fail(now, GsmFault::SimNotReady); return; }
    if (step_ == 4 && !registered_) { fail(now, GsmFault::NotRegistered); return; }
    if (polling_) { polling_ = false; state_ = GsmState::Ready; lastCheck_ = now; return; }
    if (++step_ >= sizeof(commands) / sizeof(commands[0])) {
        state_ = GsmState::Ready; lastCheck_ = now;
        fault_ = GsmFault::None;
    } else startCommand(now);
}
void GsmEngine::update(uint32_t now) {
    if (state_ == GsmState::Disabled) return;
    // Budget borne par tour : pas de boucle attendant le reseau ou un prompt.
    for (size_t n = 0; n < 192 && io_.available(); ++n) {
        const int input = io_.read(); if (input < 0) break;
        const char c = static_cast<char>(input);
        if (state_ == GsmState::Backoff) continue; // reponses tardives ignorees
        if (c == '>' && state_ == GsmState::Prompt) {
            const char ctrlZ = 26;
            state_ = GsmState::Submitting; since_ = now; used_ = 0;
            if (!io_.write(payload_, strlen(payload_)) || !io_.write(&ctrlZ, 1)) fail(now, GsmFault::WriteError);
        } else if (c == '\n') { if (used_) line(now); }
        else if (c != '\r') {
            if (used_ + 1 >= sizeof(line_)) { fail(now, GsmFault::LineTooLong); break; }
            line_[used_++] = c;
        }
    }
    if (state_ == GsmState::Backoff && now - since_ >= retryInterval_) {
        step_ = 0; startCommand(now);
    } else if ((state_ == GsmState::Initializing || state_ == GsmState::Prompt) &&
               now - since_ >= commandTimeout_) fail(now, GsmFault::PromptTimeout);
    else if (state_ == GsmState::Submitting && now - since_ >= submitTimeout_) fail(now, GsmFault::SubmitTimeout);
    else if (state_ == GsmState::Ready && (!registered_ || now - lastCheck_ >= retryInterval_)) {
        step_ = 4; polling_ = true; registered_ = false; startCommand(now);
    }
}
bool GsmEngine::submit(const char* receiver, const char* sms, uint32_t now) {
    size_t septets = 0;
    if (!ready() || !registered_ || result_ != GsmResult::None || !validE164(receiver) ||
        countD2Septets(sms, septets) != D2Error::Ok || septets == 0) return false;
    char command[40]; snprintf(command, sizeof(command), "AT+CMGS=\"%s\"\r", receiver);
    strcpy(payload_, sms); sawCmgs_ = false; used_ = 0;
    state_ = GsmState::Prompt; since_ = now;
    if (!io_.write(command, strlen(command))) fail(now, GsmFault::WriteError);
    // Meme echec immediat passe par takeResult : le service connait l'index actif.
    return true;
}
GsmResult GsmEngine::takeResult() { const GsmResult result = result_; result_ = GsmResult::None; return result; }
}
