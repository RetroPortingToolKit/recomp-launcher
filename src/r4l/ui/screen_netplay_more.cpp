// screen_netplay_more.cpp — the rest of the netplay surface: lobby server,
// list scope, password join, online players and server chat, moderation
// (report / block, moderation.ini), account sign-in, quick match, match
// tuning, spectators, memory card sharing, lobby mods and transfers, session
// variants, Link lobbies. Each block is gated by a surface key.
#include "ui.h"
#include "script.h"

#include "r4l/core/ini.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace r4l {

namespace {
const char* const kReasons[] = {RECOMP_LAUNCHER_REPORT_HARASSMENT, RECOMP_LAUNCHER_REPORT_HATE_SPEECH,
                                RECOMP_LAUNCHER_REPORT_SEXUAL_CONTENT, RECOMP_LAUNCHER_REPORT_SPAM,
                                RECOMP_LAUNCHER_REPORT_THREATS, RECOMP_LAUNCHER_REPORT_CHEATING,
                                RECOMP_LAUNCHER_REPORT_OTHER};
const char* const kReasonLabels[] = {"Harassment", "Hate speech", "Sexual content", "Spam",
                                     "Threats", "Cheating claim", "Other"};

std::string moderation_path(const Session& s) { return join_path(s.assets_dir, "moderation.ini"); }

std::string flag_text(const char* cc) { return (cc && cc[0]) ? std::string("[") + cc + "]" : std::string(); }
}  // namespace

void netplay_load_blocks(App& a) {
    a.np_blocked.clear();
    IniDoc d;
    if (!d.load(moderation_path(a.s))) return;
    std::string list = d.get("blocked", "accounts");
    size_t p = 0;
    while (p < list.size()) {
        size_t c = list.find(',', p);
        std::string t = trim(list.substr(p, c == std::string::npos ? std::string::npos : c - p));
        if (!t.empty()) a.np_blocked.push_back(t);
        if (c == std::string::npos) break;
        p = c + 1;
    }
}

void netplay_save_blocks(App& a) {
    IniDoc d;
    d.load(moderation_path(a.s));
    std::string list;
    for (const auto& b : a.np_blocked) list += (list.empty() ? "" : ",") + b;
    d.set("blocked", "accounts", list);
    d.save(moderation_path(a.s));
    const RecompLauncherCNetplayCallbacks* np = a.s.game ? a.s.game->netplay : nullptr;
    if (np && np->set_blocks) np->set_blocks(np->ctx, list.c_str());
}

// A player's context menu: report and block (moderation).
void netplay_player_menu(App& a, const char* name, const char* account) {
    if (!a.S("netplay.moderation") || !account || !account[0]) return;
    if (ImGui::BeginPopupContextItem(account)) {
        ImGui::TextDisabled("%s", name);
        if (ImGui::MenuItem(tr("Report..."))) {
            a.np_report_target = account;
            a.np_show_report = true;
        }
        bool blocked = false;
        for (const auto& b : a.np_blocked) blocked |= b == account;
        if (ImGui::MenuItem(blocked ? tr("Unblock") : tr("Block"))) {
            if (blocked) {
                for (size_t i = 0; i < a.np_blocked.size(); ++i)
                    if (a.np_blocked[i] == account) a.np_blocked.erase(a.np_blocked.begin() + static_cast<long>(i));
            } else {
                a.np_blocked.push_back(account);
            }
            netplay_save_blocks(a);
        }
        ImGui::EndPopup();
    }
}

// Pre-lobby extras: lobby server, account, quick match, online players.
void netplay_draw_outside(App& a) {
    const RecompLauncherCNetplayCallbacks* np = a.s.game->netplay;
    void* c = np->ctx;
    if (a.S("netplay.lobby_server") && np->set_lobby_url) {
        section(tr("Lobby server"));
        if (!a.np_url[0] && np->default_url) {
            const char* u = np->default_url(c);
            std::snprintf(a.np_url, sizeof a.np_url, "%s", u ? u : "");
        }
        ImGui::SetNextItemWidth(-110);
        ImGui::InputText("##url", a.np_url, sizeof a.np_url);
        ImGui::SameLine();
        if (ImGui::Button(tr("Use"), ImVec2(-1, 0))) np->set_lobby_url(c, a.np_url);
        char ip[96] = {0};
        if (np->external_ip && np->external_ip(c, ip, sizeof ip)) ImGui::TextDisabled("%s %s", tr("Internet address:"), ip);
        if (np->local_ip && np->local_ip(c, ip, sizeof ip)) ImGui::TextDisabled("%s %s", tr("Local address:"), ip);
    }
    if (a.S("netplay.account") && np->account_available && np->account_available(c)) {
        section(tr("Account"));
        const int st = np->account_state ? np->account_state(c) : 0;
        if (st == RECOMP_LAUNCHER_ACCOUNT_SIGNED_IN) {
            ImGui::Text("%s %s", tr("Signed in as"), np->account_username ? np->account_username(c) : "");
            if (np->account_handle) ImGui::TextDisabled("@%s", np->account_handle(c));
            ImGui::SetNextItemWidth(200);
            ImGui::InputTextWithHint("##handle", tr("New handle"), a.np_handle, sizeof a.np_handle);
            ImGui::SameLine();
            if (ImGui::Button(tr("Change handle")) && np->account_set_handle) np->account_set_handle(c, a.np_handle);
            ImGui::SameLine();
            if (ImGui::Button(tr("Sign out")) && np->account_sign_out) np->account_sign_out(c);
        } else if (st == RECOMP_LAUNCHER_ACCOUNT_WAITING) {
            ImGui::TextDisabled("%s", tr("Finish signing in in your browser..."));
        } else {
            if (st == RECOMP_LAUNCHER_ACCOUNT_FAILED && np->account_error)
                ImGui::TextColored(theme().bad, "%s", np->account_error(c));
            if (ImGui::Button(tr("Sign in")) && np->account_login_begin) np->account_login_begin(c);
            ImGui::SameLine();
            if (a.np_login_required)
                ImGui::TextColored(theme().warn, "%s", tr("This server only accepts signed-in players."));
            else
                ImGui::TextDisabled("%s", tr("Optional on most servers: guests can host and join."));
        }
    }
    if (a.S("netplay.automatch") && np->automatch_available && np->automatch_available(c)) {
        section(tr("Quick match"));
        const int st = np->automatch_state ? np->automatch_state(c) : 0;
        if (st == RECOMP_LAUNCHER_AUTOMATCH_IDLE || st == RECOMP_LAUNCHER_AUTOMATCH_FAILED) {
            if (st == RECOMP_LAUNCHER_AUTOMATCH_FAILED && np->automatch_error)
                ImGui::TextColored(theme().bad, "%s", np->automatch_error(c));
            const int n = np->automatch_ruleset_count ? np->automatch_ruleset_count(c) : 0;
            for (int i = 0; i < n; ++i) {
                RecompLauncherCNetplayRuleset r{};
                if (!np->automatch_ruleset_get(c, i, &r)) continue;
                ImGui::PushID(i);
                if (ImGui::Button(r.label, ImVec2(220, 0)) && np->automatch_queue) np->automatch_queue(c, r.id);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", r.caps_summary);
                ImGui::PopID();
            }
        } else if (st == RECOMP_LAUNCHER_AUTOMATCH_QUEUED) {
            ImGui::Text("%s %ds  (%d %s)", tr("Searching"), np->automatch_queued_secs ? np->automatch_queued_secs(c) : 0,
                        np->automatch_pool ? np->automatch_pool(c) : 0, tr("waiting"));
            if (ImGui::Button(tr("Cancel")) && np->automatch_cancel) np->automatch_cancel(c);
        } else if (st == RECOMP_LAUNCHER_AUTOMATCH_FOUND) {
            RecompLauncherCNetplayFound f{};
            if (np->automatch_found_get) np->automatch_found_get(c, &f);
            ImGui::Text("%s %s %s · %s · %d ms · %ds", tr("Found"), f.username, flag_text(f.country).c_str(), f.ruleset_label,
                        f.est_rtt_ms, f.accept_secs_left);
            if (big_button(tr("Accept"), ImVec2(160, 44), true) && np->automatch_accept) np->automatch_accept(c, 1);
            ImGui::SameLine();
            if (ImGui::Button(tr("Decline"), ImVec2(120, 44)) && np->automatch_accept) np->automatch_accept(c, 0);
        } else {
            ImGui::TextDisabled("%s", tr("Waiting for the other player..."));
        }
    }
    if (a.S("netplay.online") && np->online_count && np->online_get) {
        section(tr("Players online"));
        const int n = np->online_count(c);
        for (int i = 0; i < n && i < 50; ++i) {
            RecompLauncherCNetplayOnlinePlayer p{};
            if (!np->online_get(c, i, &p)) continue;
            ImGui::PushID(i + 3000);
            ImGui::Selectable((std::string(p.display_name) + " " + flag_text(p.country) +
                               (p.hosting ? std::string(" · ") + tr("hosting ") + p.lobby_name
                                          : p.in_lobby ? std::string(" · ") + tr("in a lobby") : ""))
                                  .c_str());
            script_mark((std::string("online:") + p.display_name).c_str());
            netplay_player_menu(a, p.display_name, p.account);
            ImGui::PopID();
        }
        if (np->server_chat_count && np->server_chat_get) {
            ImGui::BeginChild("##schat", ImVec2(0, 120), ImGuiChildFlags_AlwaysUseWindowPadding);
            const int cc = np->server_chat_count(c);
            for (int i = std::max(0, cc - 40); i < cc; ++i) {
                RecompLauncherCNetplayChatMessage m{};
                if (!np->server_chat_get(c, i, &m)) continue;
                bool blocked = false;
                for (const auto& b : a.np_blocked) blocked |= b == m.account;
                if (blocked) continue;
                ImGui::PushID(i + 5000);
                if (m.is_system) ImGui::TextDisabled("%s", m.text);
                else ImGui::TextWrapped("%s: %s", m.from, m.text);
                netplay_player_menu(a, m.from, m.account);
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::SetNextItemWidth(-90);
            const bool said = ImGui::InputText("##ssay", a.np_server_chat, sizeof a.np_server_chat, ImGuiInputTextFlags_EnterReturnsTrue);
            script_mark("chat:server");
            if (said && a.np_server_chat[0] && np->server_chat_send) {
                np->server_chat_send(c, a.np_server_chat);
                a.np_server_chat[0] = 0;
            }
        }
    }
}

// In-lobby extras: tuning, spectators, memory cards, mods, variant.
void netplay_draw_lobby_extras(App& a, bool host) {
    const RecompLauncherCNetplayCallbacks* np = a.s.game->netplay;
    void* c = np->ctx;
    bool caps_changed = false;
    if (a.S("netplay.tuning")) {
        section(tr("Match settings"));
        ImGui::BeginDisabled(!host);
        if (np->input_prediction_get && np->input_prediction_set) {
            int p = np->input_prediction_get(c);
            if (row_slider(tr("Input prediction"), &p, 0, 8, "%d frames")) caps_changed |= np->input_prediction_set(c, p) >= 0;
        }
        if (np->force_input_relay_get && np->force_input_relay_set) {
            int v = np->force_input_relay_get(c);
            if (row_toggle(tr("Relay input through the server"), &v)) caps_changed |= np->force_input_relay_set(c, v) >= 0;
        }
        if (np->force_turn_get && np->force_turn_set) {
            int v = np->force_turn_get(c);
            if (row_toggle(tr("Force TURN relay"), &v)) caps_changed |= np->force_turn_set(c, v) >= 0;
        }
        if (np->multitap_analog_get && np->multitap_analog_set) {
            int v = np->multitap_analog_get(c);
            if (row_toggle(tr("Analog on multitap"), &v)) caps_changed |= np->multitap_analog_set(c, v) >= 0;
        }
        if (np->relay_host_get && np->relay_host_set) {
            int v = np->relay_host_get(c);
            if (row_toggle(tr("Host through relay"), &v)) np->relay_host_set(c, v);
        }
        if (np->relay_via_ice_get && np->relay_via_ice_set) {
            // Host relay over ICE: guests reach the host without a forwarded port.
            int v = np->relay_via_ice_get(c);
            if (row_toggle(tr("Relay over ICE (no port forwarding)"), &v)) caps_changed |= np->relay_via_ice_set(c, v) >= 0;
        }
        ImGui::EndDisabled();
        char st[160] = {0};
        if (np->relay_status && np->relay_status(c, st, sizeof st) && st[0]) ImGui::TextDisabled("%s", st);
    }
    if (a.S("netplay.variant") && np->session_variant_count && np->session_variant_label) {
        const int n = np->session_variant_count(c);
        if (n > 0) {
            std::vector<const char*> labels;
            std::vector<int> values;
            int cur = 0;
            const int now = np->session_variant_get ? np->session_variant_get(c) : 0;
            for (int i = 0; i < n; ++i) {
                int v = 0;
                labels.push_back(np->session_variant_label(c, i, &v));
                values.push_back(v);
                if (v == now) cur = i;
            }
            ImGui::BeginDisabled(!host);
            if (row_combo(tr("Mode"), &cur, labels.data(), n) && np->session_variant_set) {
                np->session_variant_set(c, values[cur]);
                caps_changed = true;
            }
            ImGui::EndDisabled();
        }
    }
    if (a.S("netplay.spectators")) {
        section(tr("Spectators"));
        if (host && np->allow_spectators_get && np->allow_spectators_set) {
            int v = np->allow_spectators_get(c);
            if (row_toggle(tr("Allow spectators"), &v)) caps_changed |= np->allow_spectators_set(c, v) >= 0;
        }
        if (np->lobby_allow_spectators && np->lobby_allow_spectators(c))
            ImGui::TextDisabled("%d / %d %s", np->lobby_spectator_count ? np->lobby_spectator_count(c) : 0,
                                np->lobby_max_spectators ? np->lobby_max_spectators(c) : 0, tr("watching"));
        if (np->local_is_spectator && np->local_is_spectator(c)) chip(tr("You are watching"), theme().accent2);
        if (host && np->host_can_spectate && np->host_can_spectate(c)) ImGui::TextDisabled("%s", tr("You may watch instead of racing."));
        const int watching = np->lobby_spectator_count ? np->lobby_spectator_count(c) : 0;
        if (np->spectator_slot)
            for (int i = 0; i < watching && i < RECOMP_LAUNCHER_NETPLAY_MAX_SPECTATORS; ++i) {
                const int sl = np->spectator_slot(c, i);
                if (sl < 0) break;
                ImGui::PushID(sl + 9000);
                ImGui::TextDisabled("  %s %d", tr("Spectator seat"), sl);
                script_mark((std::string("spectator:") + std::to_string(i)).c_str());
                if (host && np->move_member) {
                    // Promotion is a move into the first open player seat.
                    int open = -1;
                    const int seats = np->lobby_max_slots ? np->lobby_max_slots(c) : 2;
                    std::vector<bool> used(seats, false);
                    for (int k = 0, n = np->member_count ? np->member_count(c) : 0; k < n; ++k) {
                        RecompLauncherCNetplayMember x{};
                        if (np->member_get && np->member_get(c, k, &x) && !x.is_spectator && x.slot >= 0 && x.slot < seats)
                            used[x.slot] = true;
                    }
                    for (int k = 0; k < seats; ++k) if (!used[k]) { open = k; break; }
                    if (open >= 0) {
                        ImGui::SameLine();
                        if (ImGui::SmallButton(tr("Seat as player"))) np->move_member(c, sl, open);
                    }
                }
                if (host && np->kick_member) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(tr("Remove"))) np->kick_member(c, sl);
                }
                ImGui::PopID();
            }
    }
    if (a.S("netplay.memcard")) {
        section(tr("Memory cards"));
        static int offer = 1, share = 0;
        if (row_toggle(tr("I have a memory card"), &offer) | row_toggle(tr("Share my card"), &share))
            if (np->memcard_offer_set) np->memcard_offer_set(c, offer, share);
        if (host && np->guest_memcard_get && np->guest_memcard_set) {
            int v = np->guest_memcard_get(c);
            if (row_toggle(tr("Use a guest's card"), &v)) np->guest_memcard_set(c, v);
        }
    }
    if (a.S("netplay.mod_transfer")) {
        const int lm = np->lobby_mods_count ? np->lobby_mods_count(c) : 0;
        const int need = np->need_mods_count ? np->need_mods_count(c) : 0;
        if (lm > 0 || need > 0) section(tr("Lobby mods"));
        for (int i = 0; i < lm; ++i) {
            RecompLauncherCNetplayLobbyMod m{};
            if (!np->lobby_mods_get(c, i, &m)) continue;
            ImGui::PushID(i + 7000);
            ImGui::Text("%s %s", m.name, m.version);
            ImGui::SameLine();
            if (m.installed) chip(tr("Installed"), theme().ok);
            else {
                chip(m.reason[0] ? m.reason : tr("Missing"), theme().warn);
                const int pr = np->lobby_mods_progress_one ? np->lobby_mods_progress_one(c, i) : -1;
                ImGui::SameLine();
                if (pr >= 0 && pr < 100) ImGui::ProgressBar(pr / 100.0f, ImVec2(120, 0));
                else if (np->lobby_mods_download_one && ImGui::SmallButton(tr("Download"))) np->lobby_mods_download_one(c, i);
            }
            ImGui::PopID();
        }
        if (np->lobby_mods_missing && np->lobby_mods_missing(c) && np->lobby_mods_can_download &&
            np->lobby_mods_can_download(c) && np->lobby_mods_download && ImGui::Button(tr("Download all")))
            np->lobby_mods_download(c);
        for (int i = 0; i < need; ++i) {
            RecompLauncherCNetplayNeedMod m{};
            if (np->need_mods_get && np->need_mods_get(c, i, &m))
                ImGui::TextDisabled("%s %s (%u KB)%s", m.name, m.version, m.size / 1024, m.builtin ? " · built-in" : "");
        }
        if (need > 0 && np->need_mods_can_transfer && np->need_mods_can_transfer(c)) {
            const int pr = np->mod_xfer_progress ? np->mod_xfer_progress(c) : -1;
            char err[160] = {0};
            if (np->mod_xfer_failed && np->mod_xfer_failed(c, err, sizeof err)) ImGui::TextColored(theme().bad, "%s", err);
            if (pr >= 0 && pr < 100) {
                ImGui::ProgressBar(pr / 100.0f, ImVec2(240, 0));
                ImGui::SameLine();
                if (ImGui::SmallButton(tr("Stop")) && np->mod_xfer_cancel) np->mod_xfer_cancel(c);
            } else if (np->mod_xfer_start && ImGui::Button(tr("Get mods from the host"))) {
                np->mod_xfer_start(c);
            }
        }
    }
    if (np->link_lobby_supported && np->link_lobby_supported(c) && np->lobby_kind_get && np->lobby_kind_set && host) {
        int k = np->lobby_kind_get(c);
        if (row_toggle(tr("Link battle (one view per player)"), &k)) caps_changed |= np->lobby_kind_set(c, k) >= 0;
    }
    if (caps_changed && host && np->push_match_caps) np->push_match_caps(c);
    if (np->local_launch_gate_set) np->local_launch_gate_set(c, a.s.media_ready() ? 1 : 0);
}

// Modals: password join, report.
void netplay_draw_modals(App& a) {
    const RecompLauncherCNetplayCallbacks* np = a.s.game->netplay;
    void* c = np->ctx;
    if (a.np_show_password) {
        ImGui::OpenPopup("lobby_password");
        a.np_show_password = false;
    }
    if (ImGui::BeginPopupModal("lobby_password", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(tr("This lobby has a password."));
        ImGui::SetNextItemWidth(260);
        ImGui::InputText("##pw2", a.np_password, sizeof a.np_password, ImGuiInputTextFlags_Password);
        script_mark("field:join_password");
        const bool join_pw = ImGui::Button(tr("Join"));
        script_mark("join:password");
        if (join_pw && np->join) {
            std::fprintf(stderr, "r4l: joining lobby %s\n", a.np_join_lobby.c_str());
            char bind[96] = {0};
            if (const char* hp = getenv("R4L_HOST_PORT")) std::snprintf(bind, sizeof bind, "0.0.0.0:%s", hp);
            if (np->join(c, a.np_join_lobby.c_str(), a.np_password, bind) < 0) a.np_status = tr("Join failed");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (a.np_show_report) {
        ImGui::OpenPopup("report");
        a.np_show_report = false;
    }
    if (ImGui::BeginPopupModal("report", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("%s", tr("Report this player's recent chat"));
        std::vector<const char*> labels;
        for (const char* l : kReasonLabels) labels.push_back(tr(l));
        row_combo(tr("Reason"), &a.np_report_reason, labels.data(), static_cast<int>(labels.size()));
        ImGui::InputTextMultiline("##note", a.np_report_note, sizeof a.np_report_note, ImVec2(360, 80));
        if (ImGui::Button(tr("Send report")) && np->chat_report) {
            std::vector<std::string> mids;
            for (int pass = 0; pass < 2; ++pass) {
                const int n = pass ? (np->server_chat_count ? np->server_chat_count(c) : 0) : (np->chat_count ? np->chat_count(c) : 0);
                for (int i = 0; i < n; ++i) {
                    RecompLauncherCNetplayChatMessage m{};
                    const bool ok = pass ? np->server_chat_get(c, i, &m) : np->chat_get(c, i, &m);
                    if (ok && a.np_report_target == m.account && m.mid[0]) mids.push_back(m.mid);
                }
            }
            std::vector<const char*> mp;
            for (auto& m : mids) mp.push_back(m.c_str());
            np->chat_report(c, mp.data(), static_cast<int>(mp.size()), kReasons[a.np_report_reason], a.np_report_note);
            a.np_status = tr("Report sent. Thank you.");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

}  // namespace r4l
