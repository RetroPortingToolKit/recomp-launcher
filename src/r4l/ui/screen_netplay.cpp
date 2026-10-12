// screen_netplay.cpp — host / join (code or address) / lobby with seats P1..Pn.
#include "ui.h"
#include "script.h"
#include "recomp_launcher_overlay.h"

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>

namespace r4l {

void netplay_draw_outside(App& a);
void netplay_draw_lobby_extras(App& a, bool host);
void netplay_draw_modals(App& a);
void netplay_load_blocks(App& a);
void netplay_player_menu(App& a, const char* name, const char* account);

// Lobby server error codes, in words a player can act on.
static std::string lobby_error_text(const char* code) {
    static const struct { const char* code; const char* text; } kWhy[] = {
        {"login_required", "This server only accepts signed-in players (Discord). Sign in to host, join or see rooms"},
        {"need_account", "Sign in to use this"},
        {"relay_unavailable", "The server could not start the match: it cannot relay a room with "
                              "spectators seated, or a guest could not reach the host"},
        {"need_players", "Waiting for another player"},
        {"missing_endpoints", "The players could not reach each other"},
        {"ice_not_connected", "The players could not reach each other"},
        {"not_host", "Only the host can start"},
        {"bad_password", "Wrong password"},
        {"lobby_full", "That room is full"},
    };
    for (const auto& w : kWhy)
        if (std::strcmp(code, w.code) == 0) return tr(w.text);
    return code;
}

void App::draw_netplay() {
    screen_title("Netplay");
    const RecompLauncherCGameInfo* g = s.game;
    const RecompLauncherCNetplayCallbacks* np = g ? g->netplay : nullptr;
    if (!g || !g->netplay_supported || !np) {
        ImGui::TextDisabled("Netplay is not available in this build.");
        return;
    }
    void* c = np->ctx;
    RecompLauncherCSettings* io = s.io;
    static bool once = false;
    if (!once) {
        once = true;
        netplay_load_blocks(*this);
        // Coming back from a match: reconnect to the room we left.
        if (g->resume_netplay_room && np->connect && !(np->connected && np->connected(c))) np->connect(c);
        if (g->resume_netplay_room && g->resume_netplay_endpoint && *g->resume_netplay_endpoint)
            std::snprintf(np_address, sizeof np_address, "%s", g->resume_netplay_endpoint);
    }
    {   // Tell the host when the player enters / leaves netplay (it may change rows).
        static int last = -1;
        const int now = (np->in_lobby && np->in_lobby(c)) ? 1 : 0;
        if (now != last && g->netplay_mode_changed) g->netplay_mode_changed(now);
        last = now;
    }
    netplay_draw_modals(*this);
    auto call_err = [&](const char* what) {
        const char* e = np->last_error ? np->last_error(c) : nullptr;
        if (e && std::strcmp(e, "login_required") == 0) np_login_required = true;
        np_status = std::string(what) + (e && *e ? ": " + lobby_error_text(e) : "");
        if (np->clear_last_error) np->clear_last_error(c);
    };

    // The lobby server answers some requests later (a refused Start, a
    // kicked join): surface whatever it said, once, in the status line.
    if (np->last_error) {
        const char* e = np->last_error(c);
        if (e && *e) {
            if (std::strcmp(e, "login_required") == 0) np_login_required = true;
            std::string msg = lobby_error_text(e);
            np_status = msg;
            if (np->clear_last_error) np->clear_last_error(c);
        }
    }

    // Identity + connection
    section("You");
    ImGui::SetNextItemWidth(320);
    if (ImGui::InputTextWithHint(tr("Player name"), "Racer", io->netplay_player_name, sizeof(io->netplay_player_name)) &&
        np->set_player_name) {
        if (np->name_rejected && np->name_rejected(c, io->netplay_player_name)) np_status = tr("That name is not allowed.");
        else np->set_player_name(c, io->netplay_player_name);
    }
    const bool connected = np->connected && np->connected(c);
    const bool connecting = np->connecting && np->connecting(c);
    ImGui::SameLine(0, 24);
    chip(connected ? "Online" : connecting ? "Connecting..." : "Offline",
         connected ? theme().ok : connecting ? theme().warn : theme().text_dim);
    if (!connected && !connecting) {
        ImGui::SameLine();
        if (ImGui::Button("Connect") && np->connect) {
            if (np->set_player_name) np->set_player_name(c, io->netplay_player_name);
            if (np->connect(c) < 0) call_err("Connect failed");
            else np_status.clear();
        }
    }
    // Sign-in, right here whenever the server needs it, whatever the title's
    // surface shows (R4 hides the Account section, but a server running with
    // DISCORD_REQUIRED is unusable without it).
    if (np->account_state && np->account_login_begin) {
        const int st = np->account_state(c);
        const bool signed_in = st == RECOMP_LAUNCHER_ACCOUNT_SIGNED_IN;
        if (!signed_in && (np_login_required || !S("netplay.account"))) {
            if (st == RECOMP_LAUNCHER_ACCOUNT_WAITING) {
                ImGui::TextColored(theme().warn, "%s", tr("Finish signing in in your browser..."));
            } else if (np_login_required) {
                if (big_button(tr("Sign in with Discord"), ImVec2(260, 44), true)) np->account_login_begin(c);
                script_mark("signin");
                if (st == RECOMP_LAUNCHER_ACCOUNT_FAILED && np->account_error)
                    ImGui::TextColored(theme().bad, "%s", np->account_error(c));
            }
        } else if (signed_in && np_login_required) {
            np_login_required = false;  // signed in: the server will take us now
            np_status.clear();
            if (np->connect && !(np->connected && np->connected(c))) np->connect(c);
        }
    }
    if (!np_status.empty()) ImGui::TextColored(theme().warn, "%s", np_status.c_str());

    const bool in_lobby = np->in_lobby && np->in_lobby(c);
    if (in_lobby) {
        // ---------------- Lobby
        const bool host = np->is_host && np->is_host(c);
        const int seats = np->lobby_max_slots ? std::max(2, np->lobby_max_slots(c)) : title->netplay_seats;
        section(host ? "Your lobby" : "Lobby");
        RecompLauncherCNetplayMember m[RECOMP_LAUNCHER_NETPLAY_MAX_MEMBERS + 1] = {};
        bool filled[RECOMP_LAUNCHER_NETPLAY_MAX_MEMBERS + 1] = {};
        const int n = np->member_count ? np->member_count(c) : 0;
        for (int i = 0; i < n; ++i) {
            RecompLauncherCNetplayMember x{};
            if (np->member_get && np->member_get(c, i, &x) && x.slot >= 0 &&
                x.slot <= RECOMP_LAUNCHER_NETPLAY_MAX_MEMBERS && !x.is_spectator) {
                m[x.slot] = x;
                filled[x.slot] = true;
            }
        }
        const float cw = (ImGui::GetContentRegionAvail().x - (seats - 1) * 10) / seats;
        for (int sl = 0; sl < seats && sl <= RECOMP_LAUNCHER_NETPLAY_MAX_MEMBERS; ++sl) {
            if (sl) ImGui::SameLine(0, 10);
            ImGui::PushID(sl);
            ImGui::BeginChild("##seat", ImVec2(cw, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
            ImGui::PushFont(theme().bold, theme().body_size * 1.3f);
            ImGui::TextColored(theme().accent, "P%d", sl + 1);
            ImGui::PopFont();
            if (filled[sl]) {
                ImGui::TextUnformatted((std::string(m[sl].display_name) + (m[sl].country[0] ? std::string(" [") + m[sl].country + "]" : "")).c_str());
                script_mark((std::string("seat:") + m[sl].display_name).c_str());
                netplay_player_menu(*this, m[sl].display_name, m[sl].account);
                if (m[sl].mod_readiness_valid && (m[sl].mods_missing || m[sl].mod_files_missing))
                    ImGui::TextColored(theme().warn, "%s", m[sl].mod_files_what[0] ? m[sl].mod_files_what : tr("missing mods"));
                if (m[sl].is_host) chip("Host", theme().accent2);
                else chip(m[sl].ready ? "Ready" : "Not ready", m[sl].ready ? theme().ok : theme().text_dim);
                if (m[sl].latency_ms > 0) ImGui::TextDisabled("%d ms", m[sl].latency_ms);
                if (host && !m[sl].is_local && np->kick_member && ImGui::SmallButton("Kick")) np->kick_member(c, sl);
                if (host && np->move_member && sl + 1 < seats && !filled[sl + 1] && ImGui::SmallButton(tr("Move down")))
                    np->move_member(c, sl, sl + 1);
                if (!m[sl].is_local && np->seat_swap_request && S("netplay.seat_swap")) {
                    if (!host) ImGui::SameLine();
                    if (ImGui::SmallButton("Swap seats") && np->seat_swap_request(c, sl) < 0)
                        call_err("Swap not possible");
                }
            } else {
                ImGui::TextDisabled("Open seat");
                if (np->seat_move_self && ImGui::SmallButton("Sit here")) np->seat_move_self(c, sl);
            }
            ImGui::EndChild();
            ImGui::PopID();
        }
        // Seat swaps: answer an incoming request, or show ours as pending.
        if (np->seat_swap_incoming) {
            char who[64] = {0};
            int from = -1;
            if (np->seat_swap_incoming(c, who, sizeof(who), &from)) {
                ImGui::TextColored(theme().accent, "%s wants to swap with you (P%d)", who[0] ? who : "A player", from + 1);
                ImGui::SameLine();
                if (ImGui::SmallButton("Accept") && np->seat_swap_respond) np->seat_swap_respond(c, 1);
                ImGui::SameLine();
                if (ImGui::SmallButton("Decline") && np->seat_swap_respond) np->seat_swap_respond(c, 0);
            } else if (np->seat_swap_outgoing && np->seat_swap_outgoing(c)) {
                ImGui::TextDisabled("Swap requested, waiting for an answer...");
                ImGui::SameLine();
                if (ImGui::SmallButton("Cancel") && np->seat_swap_clear) np->seat_swap_clear(c);
            }
        }
        ImGui::Dummy(ImVec2(0, 8));
        if (!host && np->set_ready) {
            const bool r = np->local_ready && np->local_ready(c);
            if (big_button(r ? "Ready!" : "Ready up", ImVec2(200, 52), r)) np->set_ready(c, r ? 0 : 1);
            ImGui::SameLine();
        }
        if (host && np->request_start) {
            // all_ready() is the lobby server's view; a LAN / Direct-IP room has
            // no server, so fall back to the seated members' own ready flags.
            int seated = 0, ready = 0;
            for (int sl = 0; sl <= RECOMP_LAUNCHER_NETPLAY_MAX_MEMBERS; ++sl)
                if (filled[sl]) { ++seated; ready += m[sl].ready ? 1 : 0; }
            const bool all = (np->all_ready && np->all_ready(c)) || (seated >= 2 && ready == seated);
            ImGui::BeginDisabled(!all || !s.media_ready());
            if (big_button("Start race", ImVec2(200, 52), true) && np->request_start(c, io) < 0) call_err("Start failed");
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (ImGui::Button("Leave", ImVec2(120, 52)) && np->leave) np->leave(c);
        // Repeat the status beside the buttons that cause it (the one at the
        // top of the page is out of view once a lobby is open).
        if (!np_status.empty()) ImGui::TextColored(theme().warn, "%s", np_status.c_str());
        if (host && np->input_delay_get && np->input_delay_set) {
            int d = np->input_delay_get(c);
            if (row_slider("Input delay", &d, 0, 8, "%d frames")) np->input_delay_set(c, d);
        }
        if (host && np->rollback_get && np->rollback_set) {
            int r = np->rollback_get(c);
            if (row_toggle("Rollback", &r)) np->rollback_set(c, r);
        }
        // Chat
        netplay_draw_lobby_extras(*this, host);
        if (S("netplay.chat") && np->chat_count && np->chat_get) {
            section("Chat");
            ImGui::BeginChild("##chat", ImVec2(0, 140), ImGuiChildFlags_AlwaysUseWindowPadding);
            const int cc = np->chat_count(c);
            for (int i = std::max(0, cc - 50); i < cc; ++i) {
                RecompLauncherCNetplayChatMessage msg{};
                if (!np->chat_get(c, i, &msg)) continue;
                bool blocked = false;
                for (const auto& b : np_blocked) blocked |= b == msg.account;
                if (blocked) continue;
                ImGui::PushID(i);
                if (msg.is_system) ImGui::TextDisabled("%s", msg.text);
                else ImGui::TextWrapped("%s: %s", msg.from, msg.text);
                netplay_player_menu(*this, msg.from, msg.account);
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::SetNextItemWidth(-90);
            const bool said = ImGui::InputText("##say", np_chat, sizeof(np_chat), ImGuiInputTextFlags_EnterReturnsTrue);
            script_mark("chat:say");
            if (said && np_chat[0]) {
                if (np->chat_send) np->chat_send(c, np_chat);
                np_chat[0] = 0;
            }
        }
        // Everyone launches together once the host's start goes through.
        if (mode == Mode::Overlay && np->launch_pending && np->launch_pending(c)) {
            // In-game menu: end this session; the launcher loop the runtime
            // returns to starts the match (run_window's in-game path).
            if (!exit_requested && this->host && this->host->request_exit) {
                exit_requested = true;
                std::string err;
                s.persist_media(&err);
                s.commit_files(&err);
                np_status = "Starting the match...";
                this->host->request_exit(this->host->ctx, 1);
            }
        } else if (np->launch_pending && np->launch_pending(c) && np->fill_launch) {
            RecompLauncherCNetplayLaunch l{};
            if (np->fill_launch(c, &l) && l.enabled) {
                std::string err;
                if (g->mods && g->mods->commit_netplay &&
                    !g->mods->commit_netplay(g->mods->ctx, s.primary_disc().c_str())) {
                    np_status = "Mods could not be prepared for this match.";
                } else {
                    io->netplay_launch = l;
                    s.persist_media(&err);
                    s.commit_files(&err);
                    s.outcome = Outcome::Launch;
                }
            }
            if (np->clear_launch_pending) np->clear_launch_pending(c);
        }
        return;
    }

    // ---------------- Host / Join
    const bool show_host = S("netplay.host"), show_join = S("netplay.join");
    const float half = (show_host && show_join) ? (ImGui::GetContentRegionAvail().x - 16) * 0.5f : -1.0f;
    if (show_host) {
    ImGui::BeginChild("##host", ImVec2(half, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
    ImGui::PushFont(theme().bold, theme().body_size * 1.2f);
    ImGui::TextUnformatted("Host a race");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##ln", "Lobby name", np_lobby_name, sizeof(np_lobby_name));
    script_mark("field:lobby_name");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##pw", "Password (optional)", np_password, sizeof(np_password),
                             ImGuiInputTextFlags_Password);
    script_mark("field:password");
    static const char* kSeats[] = {"2 players", "3 players", "4 players"};
    int si = np_seats - 2;
    int max_seats = title->netplay_seats;
    if (np->create_max_slots) max_seats = std::min(max_seats, std::max(2, np->create_max_slots(c, np_lan_only ? 1 : 0)));
    if (row_combo(tr("Seats"), &si, kSeats, std::max(1, std::min(3, max_seats - 1)))) np_seats = si + 2;
    int lan = np_lan_only ? 1 : 0;
    if (row_toggle("LAN only", &lan)) np_lan_only = lan != 0;
    // Applied by the next create, so it belongs on the form (LAN rooms have no gallery).
    if (!np_lan_only && S("netplay.spectators") && np->allow_spectators_get && np->allow_spectators_set) {
        int v = np->allow_spectators_get(c);
        if (row_toggle(tr("Allow spectators"), &v)) np->allow_spectators_set(c, v);
    }
    ImGui::BeginDisabled(!s.media_ready());
    if (big_button("Host", ImVec2(-1, 48), true) && np->create) {
        char endpoint[96] = {0};
        // Test hook: host on an explicit port (several instances on one machine).
        if (const char* hp = getenv("R4L_HOST_PORT")) std::snprintf(endpoint, sizeof endpoint, "0.0.0.0:%s", hp);
        if (np->set_player_name) np->set_player_name(c, io->netplay_player_name);
        const int rc = np->create(c, np_lobby_name, endpoint, np_password, io, np_lan_only ? 1 : 0, np_seats);
        if (rc < 0) call_err(rc == -4 ? "Port is busy" : "Could not host");
        else if (np->create_default_rollback && np->rollback_set)
            np->rollback_set(c, np->create_default_rollback(c, np_seats));
    }
    ImGui::EndDisabled();
    ImGui::EndChild();
    }
    if (show_host && show_join) ImGui::SameLine(0, 16);
    if (show_join) {
    ImGui::BeginChild("##join", ImVec2(half, 0), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_AutoResizeY);
    ImGui::PushFont(theme().bold, theme().body_size * 1.2f);
    ImGui::TextUnformatted("Join a race");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-110);
    ImGui::InputTextWithHint("##code", "Lobby code", np_join_code, sizeof(np_join_code));
    script_mark("field:code");
    ImGui::SameLine();
    const bool join_code = ImGui::Button("Join##code", ImVec2(-1, 0));
    script_mark("join:code");
    if (join_code && np->join && np_join_code[0]) {
        char bind[96] = {0};
        if (const char* hp = getenv("R4L_HOST_PORT")) std::snprintf(bind, sizeof bind, "0.0.0.0:%s", hp);
        if (np->join(c, np_join_code, np_password, bind) < 0) call_err("Join failed");
    }
    ImGui::SetNextItemWidth(-110);
    ImGui::InputTextWithHint("##addr", "Address (host:port)", np_address, sizeof(np_address));
    script_mark("field:address");
    ImGui::SameLine();
    const bool join_addr = ImGui::Button("Join##addr", ImVec2(-1, 0));
    script_mark("join:address");
    if (join_addr && np->join && np_address[0]) {
        char bind[96] = {0};
        if (const char* hp = getenv("R4L_HOST_PORT")) std::snprintf(bind, sizeof bind, "0.0.0.0:%s", hp);
        const std::string id = std::string("lan:") + np_address;
        const int rc = np->join(c, id.c_str(), np_password, bind);
        if (rc < 0) call_err(rc == -3 ? "No host answered at that address" : rc == -2 ? "Wrong password" : "Join failed");
    }
    if (np->local_address_get) {
        ImGui::TextDisabled("Your addresses:");
        for (int i = 0; i < 4; ++i) {
            RecompLauncherCNetplayLocalAddress a{};
            if (!np->local_address_get(c, i, &a)) break;
            ImGui::TextDisabled("  %s  %s", a.address, a.label);
        }
    }
    ImGui::EndChild();
    }
    netplay_draw_outside(*this);
    if (S("netplay.browse") && connected && np->list_count && np->list_get) {
        section(tr("Open lobbies"));
        if (np->list_scope_set) {
            static const char* kScope[] = {"All", "LAN", "Online"};
            for (int k = 0; k < 3; ++k) {
                if (k) ImGui::SameLine();
                if (ImGui::RadioButton(tr(kScope[k]), np_list_scope == k)) {
                    np_list_scope = k;
                    np->list_scope_set(c, k);
                    if (np->request_list) np->request_list(c);
                }
            }
            ImGui::SameLine(0, 20);
        }
        if (ImGui::Button(tr("Refresh")) && np->request_list) np->request_list(c);
        const int n = np->list_count(c);
        if (!n) ImGui::TextDisabled("No lobbies right now. Host one!");
        for (int i = 0; i < n; ++i) {
            RecompLauncherCNetplayLobby l{};
            if (!np->list_get(c, i, &l)) continue;
            ImGui::PushID(i);
            char row[200];
            // "~" marks a through-the-server estimate, not a timed direct link.
            if (l.latency_ms < 0)
                std::snprintf(row, sizeof(row), "%s   %d/%d   %s", l.name, l.player_count, l.max_slots,
                              l.has_password ? "locked" : "");
            else
                std::snprintf(row, sizeof(row), "%s   %d/%d   %s%s%d ms", l.name, l.player_count, l.max_slots,
                              l.has_password ? "locked  " : "", l.latency_is_estimate ? "~" : "", l.latency_ms);
            const bool picked = ImGui::Selectable(row, false, 0, ImVec2(0, theme().row_h));
            script_mark((std::string("lobby:") + l.name).c_str());
            if (picked && np->join) {
                if (l.has_password) {
                    np_join_lobby = l.lobby_id;
                    np_show_password = true;
                } else {
                    char bind[96] = {0};
        if (const char* hp = getenv("R4L_HOST_PORT")) std::snprintf(bind, sizeof bind, "0.0.0.0:%s", hp);
                    std::fprintf(stderr, "r4l: joining lobby %s\n", l.lobby_id);
                    if (np->join(c, l.lobby_id, np_password, bind) < 0) call_err("Join failed");
                }
            }
            ImGui::PopID();
        }
    }
}

}  // namespace r4l
