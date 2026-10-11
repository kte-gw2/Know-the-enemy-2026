/*
 * Know thy enemy - arcdps extension
 *
 * Counts the enemy players (and their elite specializations) that your squad
 * hits or is hit by during a fight, grouped by WvW team.
 *
 * This version only uses the public arcdps combat API and MumbleLink. Earlier
 * releases read arcdps/GW2 memory through closed-source offsets, which broke on
 * every game/arcdps update, and were built against ImGui 1.80, which arcdps no
 * longer uses (arcdps moved to ImGui 1.92 in 2026).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mutex>
#include <algorithm>
#include <string>
#include <fstream>
#include <array>
#include <vector>
#include <unordered_set>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <ctime>

#include "imgui/imgui.h"
#include "arcdps_api.h"

/* ---------------------------------------------------------------------------
 * Professions / elite specializations
 * prof ids: 1 Guardian, 2 Warrior, 3 Engineer, 4 Ranger, 5 Thief,
 *           6 Elementalist, 7 Mesmer, 8 Necromancer, 9 Revenant
 * elite ids come from https://api.guildwars2.com/v2/specializations
 * ------------------------------------------------------------------------- */
constexpr int NUM_PROFS = 9;
constexpr int SLOTS_PER_PROF = 5; // core + 4 elite specs
constexpr int NUM_SPECS = NUM_PROFS * SLOTS_PER_PROF;

struct spec_info {
	const char* name;
	const char* short_name;
	uint32_t elite; // 0 == core
};

const spec_info spec_lut[NUM_SPECS] = {
	// Guardian
	{"Guardian", "Gdn", 0}, {"Dragonhunter", "Dgh", 27}, {"Firebrand", "Fbd", 62}, {"Willbender", "Wbd", 65}, {"Luminary", "Lum", 81},
	// Warrior
	{"Warrior", "War", 0}, {"Berserker", "Brs", 18}, {"Spellbreaker", "Spb", 61}, {"Bladesworn", "Bds", 68}, {"Paragon", "Par", 74},
	// Engineer
	{"Engineer", "Eng", 0}, {"Scrapper", "Scr", 43}, {"Holosmith", "Hls", 57}, {"Mechanist", "Mec", 70}, {"Amalgam", "Amg", 75},
	// Ranger
	{"Ranger", "Rgr", 0}, {"Druid", "Dru", 5}, {"Soulbeast", "Slb", 55}, {"Untamed", "Unt", 72}, {"Galeshot", "Gal", 78},
	// Thief
	{"Thief", "Thf", 0}, {"Daredevil", "Dar", 7}, {"Deadeye", "Ded", 58}, {"Specter", "Spe", 71}, {"Antiquary", "Ant", 77},
	// Elementalist
	{"Elementalist", "Ele", 0}, {"Tempest", "Tmp", 48}, {"Weaver", "Wea", 56}, {"Catalyst", "Cat", 67}, {"Evoker", "Evo", 80},
	// Mesmer
	{"Mesmer", "Mes", 0}, {"Chronomancer", "Chr", 40}, {"Mirage", "Mir", 59}, {"Virtuoso", "Vir", 66}, {"Troubadour", "Tro", 73},
	// Necromancer
	{"Necromancer", "Nec", 0}, {"Reaper", "Rea", 34}, {"Scourge", "Scg", 60}, {"Harbinger", "Har", 64}, {"Ritualist", "Rit", 76},
	// Revenant
	{"Revenant", "Rev", 0}, {"Herald", "Her", 52}, {"Renegade", "Ren", 63}, {"Vindicator", "Vin", 69}, {"Conduit", "Con", 79},
};

/* returns index into spec_lut, or -1. unknown (future) elite specs count as the core profession */
int spec_index(uint32_t prof, uint32_t elite)
{
	if (prof < 1 || prof > NUM_PROFS)
		return -1;
	int base = (int)(prof - 1) * SLOTS_PER_PROF;
	for (int i = 1; i < SLOTS_PER_PROF; i++)
	{
		if (spec_lut[base + i].elite == elite)
			return base + i;
	}
	return base;
}

/* ---------------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------------- */
constexpr uint8_t MAX_HISTORY_SIZE = 8; // power of 2

struct team_battle {
	std::array<uint16_t, NUM_SPECS> counts{};
	uint16_t total = 0;
	void clear() { counts.fill(0); total = 0; }
};

struct team_history {
	uint16_t team_id;
	std::array<team_battle, MAX_HISTORY_SIZE> battles;
};

enum class combat_state { OUT_BATTLE, IN_BATTLE };
enum class display_state { NONE, BATTLE, HISTORY_TAB, HISTORY_COL };

struct custom_team {
	char name[32];
	int32_t id;
};

constexpr uint32_t SETTINGS_MAGIC = 0xC2FFEE;
struct settings {
	uint32_t magic = SETTINGS_MAGIC;
	bool bEnabled = true;
	bool bTitleBg = true;
	bool bShowColumns = false;
	bool bShortNames = false;
	bool bHideInCombat = false;
	bool bOnlyWvw = true;
	ImGuiWindowFlags wFlags = ImGuiWindowFlags_NoCollapse;
	int32_t red_team = 0;
	int32_t green_team = 0;
	int32_t blue_team = 0;
	custom_team cteam1 = {};
	custom_team cteam2 = {};
	custom_team cteam3 = {};
};

std::mutex mtx; // guards everything below that the combat thread touches

std::vector<team_history> histories;
std::array<std::array<char, 32>, MAX_HISTORY_SIZE> history_labels{};
std::unordered_set<uintptr_t> seen_ids;     // enemies already counted this fight
std::unordered_set<uintptr_t> squad_ids;    // agent ids of squad/party members
uint16_t my_team_id = 0;

int cur_history_idx = 0;
int history_to_disp_idx = 0;
int history_radio_state = 0;
uint16_t tab_team_idx = 0;
bool override_tab_max_switch = false;

combat_state cmb_state = combat_state::OUT_BATTLE;
DWORD logstart_time = 0;
DWORD last_cbt_evt = 0;
bool fight_ended = true; // next counted enemy starts a new history entry

settings kte_settings;
bool reset_pos = false;
bool mod_key1 = false;
bool mod_key2 = false;

/* arcdps exports */
HMODULE arcdll = nullptr;
void (*arclog)(const char*) = nullptr;
void (*arccolors)(ImVec4**) = nullptr;
const wchar_t* (*get_settings_path)() = nullptr;
uint64_t (*get_ui_settings)() = nullptr;
uint64_t (*get_key_settings)() = nullptr;
void* (*arc_malloc)(size_t) = nullptr;
void (*arc_free)(void*) = nullptr;

ImVec4* color_array[5] = {};
const ImVec4 fallback_color(0.5f, 0.5f, 0.5f, 1.0f);

void log_arc(const char* str)
{
	if (arclog)
		arclog(str);
}

/* ---------------------------------------------------------------------------
 * MumbleLink: used only to know whether we are on a WvW map
 * ------------------------------------------------------------------------- */
struct mumble_linked_mem {
	uint32_t uiVersion;
	uint32_t uiTick;
	float fAvatarPosition[3];
	float fAvatarFront[3];
	float fAvatarTop[3];
	wchar_t name[256];
	float fCameraPosition[3];
	float fCameraFront[3];
	float fCameraTop[3];
	wchar_t identity[256];
	uint32_t context_len;
	unsigned char context[256];
	wchar_t description[2048];
};
HANDLE mumble_handle = nullptr;
const mumble_linked_mem* mumble = nullptr;

void open_mumble()
{
	if (mumble)
		return;
	mumble_handle = OpenFileMappingW(FILE_MAP_READ, FALSE, L"MumbleLink");
	if (mumble_handle)
		mumble = (const mumble_linked_mem*)MapViewOfFile(mumble_handle, FILE_MAP_READ, 0, 0, sizeof(mumble_linked_mem));
}

bool is_wvw_map()
{
	open_mumble();
	if (!mumble || mumble->uiTick == 0)
		return true; // can't tell, don't hide anything
	uint32_t map_type;
	memcpy(&map_type, mumble->context + 32, sizeof(map_type)); // serverAddress[28], mapId, mapType
	// 9 Eternal Battlegrounds, 10-12 Borderlands, 13 Fortune's Vale, 14 Obsidian Sanctum, 15 Edge of the Mists, 18 Armistice Bastion
	return (map_type >= 9 && map_type <= 15) || map_type == 18;
}

/* ---------------------------------------------------------------------------
 * Counting
 * ------------------------------------------------------------------------- */
void end_fight_locked()
{
	if (cmb_state == combat_state::IN_BATTLE || !fight_ended)
	{
		char* label = history_labels[cur_history_idx].data();
		time_t now = time(nullptr);
		tm local{};
		localtime_s(&local, &now);
		strftime(label, 32, "%H:%M:%S", &local);
		snprintf(label + 8, 32 - 8, " (%lus)", (unsigned long)((GetTickCount() - logstart_time) / 1000));
	}
	cmb_state = combat_state::OUT_BATTLE;
	fight_ended = true;
}

void start_new_history_locked()
{
	bool all_zero = true;
	for (auto& team : histories)
		all_zero = all_zero && team.battles[cur_history_idx].total == 0;
	if (!all_zero)
	{
		cur_history_idx = (cur_history_idx + 1) & (MAX_HISTORY_SIZE - 1);
		for (auto& team : histories)
			team.battles[cur_history_idx].clear();
		history_labels[cur_history_idx][0] = 0;
	}
	history_to_disp_idx = cur_history_idx;
	history_radio_state = 0;
	override_tab_max_switch = false;
	seen_ids.clear();
	logstart_time = GetTickCount();
}

void register_enemy_locked(const ag* enemy)
{
	int idx = spec_index(enemy->prof, enemy->elite);
	if (idx < 0)
		return;

	if (fight_ended)
	{
		fight_ended = false;
		start_new_history_locked();
	}
	cmb_state = combat_state::IN_BATTLE;
	last_cbt_evt = GetTickCount();

	if (!seen_ids.insert(enemy->id).second)
		return; // already counted

	team_history* team = nullptr;
	for (auto& t : histories)
	{
		if (t.team_id == enemy->team)
		{
			team = &t;
			break;
		}
	}
	if (!team)
	{
		histories.push_back(team_history{enemy->team, {}});
		team = &histories.back();
	}
	team_battle& b = team->battles[cur_history_idx];
	b.counts[idx]++;
	b.total++;
}

bool is_player(const ag* a)
{
	return a && a->elite != 0xFFFFFFFF && a->prof >= 1 && a->prof <= NUM_PROFS;
}

bool is_ours(const ag* a)
{
	if (a->self || squad_ids.count(a->id))
		return true;
	return my_team_id != 0 && a->team == my_team_id;
}

bool is_damage_event(const cbtevent* ev)
{
	if (ev->is_statechange || ev->is_activation || ev->is_buffremove)
		return false;
	if (ev->buff)
		return ev->buff_dmg > 0 && ev->value == 0; // condition tick
	return ev->value > 0; // strike
}

/* area combat callback -- may be called asynchronously */
void mod_combat(cbtevent* ev, ag* src, ag* dst, const char* skillname, uint64_t id, uint64_t revision)
{
	std::lock_guard<std::mutex> lock(mtx);

	if (!ev)
	{
		/* agent notifications: src->elite == 0 means squad/party add (prof != 0) or remove (prof == 0) */
		if (src && dst && src->elite == 0)
		{
			if (src->prof != 0)
			{
				squad_ids.insert(src->id);
				if (dst->self && dst->team != 0)
					my_team_id = dst->team;
			}
			else
			{
				squad_ids.erase(src->id);
			}
		}
		return;
	}

	if (!kte_settings.bEnabled)
		return;

	if (ev->is_statechange == CBTS_SQCOMBATSTART)
	{
		logstart_time = GetTickCount();
		return;
	}
	if (ev->is_statechange == CBTS_SQCOMBATEND)
	{
		end_fight_locked();
		return;
	}

	if (!is_player(src) || !is_player(dst) || !is_damage_event(ev))
		return;

	if (src->self && src->team)
		my_team_id = src->team;
	else if (dst->self && dst->team)
		my_team_id = dst->team;

	bool src_ours = is_ours(src);
	bool dst_ours = is_ours(dst);
	if (src_ours == dst_ours)
		return;
	const ag* enemy = src_ours ? dst : src;
	if (my_team_id != 0 && enemy->team == my_team_id)
		return;

	if (kte_settings.bOnlyWvw && !is_wvw_map())
		return;

	register_enemy_locked(enemy);
}

/* ---------------------------------------------------------------------------
 * UI
 * ------------------------------------------------------------------------- */
uintptr_t mod_wnd(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (get_ui_settings && get_key_settings && ((get_ui_settings() >> 2) & 1))
	{
		kte_settings.wFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
		uint64_t keys = get_key_settings();
		uint16_t key1 = (uint16_t)(keys & 0xFFFF);
		uint16_t key2 = (uint16_t)((keys >> 16) & 0xFFFF);
		if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN)
		{
			if (wParam == key1) mod_key1 = true;
			if (wParam == key2) mod_key2 = true;
		}
		else if (uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP)
		{
			if (wParam == key1) mod_key1 = false;
			if (wParam == key2) mod_key2 = false;
		}
		if (mod_key1 && mod_key2)
			kte_settings.wFlags &= ~(ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
	}
	else
		kte_settings.wFlags &= ~(ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
	return uMsg;
}

void team_name(char* buf, uint16_t team_id)
{
	const custom_team* custom[] = {&kte_settings.cteam1, &kte_settings.cteam2, &kte_settings.cteam3};
	for (auto* c : custom)
	{
		if (c->id != 0 && c->id == team_id && c->name[0])
		{
			snprintf(buf, 32, "%s", c->name);
			return;
		}
	}
	if (team_id == 0)
		snprintf(buf, 32, "Enemies");
	else if (kte_settings.blue_team == team_id)
		snprintf(buf, 32, "Blue");
	else if (kte_settings.green_team == team_id)
		snprintf(buf, 32, "Green");
	else if (kte_settings.red_team == team_id)
		snprintf(buf, 32, "Red");
	else
		snprintf(buf, 32, "Team %d", team_id);
}

void options_end_proc()
{
	ImGui::Checkbox("Know thy enemy##1cb", &kte_settings.bEnabled);
	ImGui::Checkbox("Only in WvW##kteonlywvw", &kte_settings.bOnlyWvw);

	ImGui::PushItemWidth(80);
	ImGui::InputInt("Red team id##redteamid", (int*)&kte_settings.red_team, 0, 0);
	ImGui::InputInt("Green team id##greenteamid", (int*)&kte_settings.green_team, 0, 0);
	ImGui::InputInt("Blue team id##blueteamid", (int*)&kte_settings.blue_team, 0, 0);
	ImGui::PopItemWidth();

	custom_team* custom[] = {&kte_settings.cteam1, &kte_settings.cteam2, &kte_settings.cteam3};
	for (int i = 0; i < 3; i++)
	{
		ImGui::PushID(i);
		ImGui::PushItemWidth(120);
		ImGui::InputText("##cteamname", custom[i]->name, sizeof(custom[i]->name));
		ImGui::PopItemWidth();
		ImGui::SameLine();
		ImGui::PushItemWidth(80);
		ImGui::InputInt("custom team id##cteamid", (int*)&custom[i]->id, 0, 0);
		ImGui::PopItemWidth();
		ImGui::PopID();
	}

	{
		std::lock_guard<std::mutex> lock(mtx);
		if (!histories.empty())
		{
			ImGui::TextUnformatted("Seen team ids:");
			for (auto& t : histories)
			{
				ImGui::SameLine();
				ImGui::Text("%u", t.team_id);
			}
		}
	}

	ImGui::Separator();
	if (ImGui::Button("Reset position"))
		reset_pos = true;
	ImGui::SameLine();
	if (ImGui::Button("Reset settings"))
		kte_settings = settings();
}

int options_windows_proc(const char* windowname)
{
	return 0;
}

void draw_bar(float frac, const char* text, const ImVec4& color)
{
	ImVec2 upper_left = ImGui::GetCursorScreenPos();
	ImVec2 lower_right(upper_left.x + ImGui::GetContentRegionAvail().x * frac, upper_left.y + ImGui::GetTextLineHeight() + 2);
	ImGui::GetWindowDrawList()->AddRectFilled(upper_left, lower_right, ImGui::ColorConvertFloat4ToU32(color));

	ImVec2 text_start = ImGui::GetCursorPos();
	text_start.y += 1;
	const float inner_pad = 5;
	text_start.x += inner_pad;
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, .6f));
	ImGui::SetCursorPos(ImVec2(text_start.x + 1, text_start.y + 1));
	ImGui::TextUnformatted(text);
	ImGui::PopStyleColor();

	ImGui::SetCursorPos(text_start);
	ImGui::TextUnformatted(text);
	text_start.x -= inner_pad;
	text_start.y += ImGui::GetTextLineHeight() + 3;
	ImGui::SetCursorPos(text_start);
	ImGui::Dummy(ImVec2(0, 0)); // 1.89+: extend window boundaries after SetCursorPos
}

const ImVec4& prof_color(int prof)
{
	if (color_array[1])
		return color_array[1][prof];
	return fallback_color;
}

void draw_team_bars(const team_battle& battle)
{
	const ImVec4 total_color = ImGui::GetStyleColorVec4(ImGuiCol_PlotHistogram);
	char temp[32];
	snprintf(temp, sizeof(temp), kte_settings.bShortNames ? "%02d Tot" : "%02d Total", battle.total);
	if (battle.total == 0)
	{
		draw_bar(1.f, temp, total_color);
		return;
	}

	std::array<int, NUM_SPECS> order;
	for (int i = 0; i < NUM_SPECS; i++)
		order[i] = i;
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return battle.counts[a] > battle.counts[b]; });

	if (color_array[0])
		ImGui::PushStyleColor(ImGuiCol_Text, color_array[0][4]);
	else
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));

	draw_bar(1.f, temp, total_color);
	float cur_max = (float)battle.counts[order[0]];
	for (int idx : order)
	{
		uint16_t count = battle.counts[idx];
		if (count == 0)
			break;
		snprintf(temp, sizeof(temp), "%02d %s", count, kte_settings.bShortNames ? spec_lut[idx].short_name : spec_lut[idx].name);
		draw_bar((float)count / cur_max, temp, prof_color(idx / SLOTS_PER_PROF + 1));
	}
	ImGui::PopStyleColor();
}

void draw_history_menu()
{
	if (histories.empty())
	{
		ImGui::TextUnformatted("No data...");
		return;
	}
	if (ImGui::RadioButton("Current", history_radio_state == 0))
	{
		history_radio_state = 0;
		history_to_disp_idx = cur_history_idx;
		ImGui::CloseCurrentPopup();
	}
	int past = (cur_history_idx - 1 + MAX_HISTORY_SIZE) & (MAX_HISTORY_SIZE - 1);
	for (int i = 0; i < MAX_HISTORY_SIZE - 1; i++)
	{
		if (history_labels[past][0])
		{
			ImGui::PushID(i);
			if (ImGui::RadioButton(history_labels[past].data(), history_radio_state == i + 1))
			{
				history_radio_state = i + 1;
				history_to_disp_idx = past;
				ImGui::CloseCurrentPopup();
			}
			ImGui::PopID();
		}
		past = (past - 1 + MAX_HISTORY_SIZE) & (MAX_HISTORY_SIZE - 1);
	}
}

void draw_style_menu()
{
	bool bTitlebar = (kte_settings.wFlags & ImGuiWindowFlags_NoTitleBar) == 0;
	if (ImGui::Checkbox("title bar", &bTitlebar))
		kte_settings.wFlags ^= ImGuiWindowFlags_NoTitleBar;
	bool bScrollbar = (kte_settings.wFlags & ImGuiWindowFlags_NoScrollbar) == 0;
	if (ImGui::Checkbox("scroll bar", &bScrollbar))
		kte_settings.wFlags ^= ImGuiWindowFlags_NoScrollbar;
	bool bBackground = (kte_settings.wFlags & ImGuiWindowFlags_NoBackground) == 0;
	if (ImGui::Checkbox("background", &bBackground))
		kte_settings.wFlags ^= ImGuiWindowFlags_NoBackground;
	ImGui::Checkbox("title bar background", &kte_settings.bTitleBg);
	ImGui::Checkbox("use columns", &kte_settings.bShowColumns);
	ImGui::Checkbox("use short names", &kte_settings.bShortNames);
	ImGui::Checkbox("hide while in combat", &kte_settings.bHideInCombat);
	ImGui::Checkbox("only in WvW", &kte_settings.bOnlyWvw);
}

void draw_teams_tabbed()
{
	const ImGuiStyle& style = ImGui::GetStyle();
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 1));
	if (!override_tab_max_switch)
	{
		uint16_t cur_max = 0;
		for (size_t i = 0; i < histories.size(); i++)
		{
			if (histories[i].battles[history_to_disp_idx].total > cur_max)
			{
				cur_max = histories[i].battles[history_to_disp_idx].total;
				tab_team_idx = (uint16_t)i;
			}
		}
	}
	if (tab_team_idx >= histories.size())
		tab_team_idx = 0;

	for (size_t i = 0; i < histories.size(); i++)
	{
		ImU32 col = ImGui::GetColorU32(i == tab_team_idx ? ImGuiCol_TabSelected : ImGuiCol_Tab);
		ImGui::PushStyleColor(ImGuiCol_Button, col);
		char temp[48];
		team_name(temp, histories[i].team_id);
		ImGui::PushID((int)i);
		if (ImGui::Button(temp))
		{
			override_tab_max_switch = true;
			tab_team_idx = (uint16_t)i;
		}
		ImGui::PopID();
		ImGui::PopStyleColor();
		ImGui::SameLine();
	}
	ImGui::NewLine();
	ImGui::Separator();
	draw_team_bars(histories[tab_team_idx].battles[history_to_disp_idx]);
	ImGui::PopStyleVar();
}

void draw_teams_columns()
{
	const ImGuiStyle& style = ImGui::GetStyle();
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 1));
	int n = (int)std::min<size_t>(histories.size(), 64);
	if (ImGui::BeginTable("coltable", n, ImGuiTableFlags_BordersInner | ImGuiTableFlags_NoPadOuterX))
	{
		char temp[48];
		for (int i = 0; i < n; i++)
		{
			team_name(temp, histories[i].team_id);
			ImGui::TableSetupColumn(temp, ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_WidthStretch);
		}
		ImGui::TableHeadersRow();
		ImGui::TableNextRow();
		for (int i = 0; i < n; i++)
		{
			ImGui::TableSetColumnIndex(i);
			draw_team_bars(histories[i].battles[history_to_disp_idx]);
		}
		ImGui::EndTable();
	}
	ImGui::PopStyleVar();
}

void centered_text(const char* text)
{
	float win_width = ImGui::GetWindowWidth();
	ImGui::SetCursorPosX(std::max(0.f, win_width * 0.5f - ImGui::CalcTextSize(text).x * 0.5f));
	ImGui::TextUnformatted(text);
}

void draw_battle_wait()
{
	ImGui::NewLine();
	const char* text = "BATTLE IN PROGRESS";
	if (ImGui::CalcTextSize(text).x + 4 >= ImGui::GetWindowWidth())
	{
		centered_text("BATTLE");
		centered_text("IN");
		centered_text("PROGRESS");
	}
	else
		centered_text(text);
	ImGui::NewLine();
	char timer[32];
	unsigned secs = (unsigned)((GetTickCount() - logstart_time) / 1000);
	snprintf(timer, sizeof(timer), "%02u:%02u", secs / 60, secs % 60);
	centered_text(timer);
}

display_state get_display_state()
{
	if (histories.empty())
		return display_state::NONE;
	if (cmb_state == combat_state::IN_BATTLE && cur_history_idx == history_to_disp_idx)
		return display_state::BATTLE;
	return kte_settings.bShowColumns ? display_state::HISTORY_COL : display_state::HISTORY_TAB;
}

void imgui_proc(uint32_t not_charsel_or_loading, uint32_t hide_if_combat_or_ooc)
{
	if (!not_charsel_or_loading || !kte_settings.bEnabled)
		return;
	if (kte_settings.bOnlyWvw && !is_wvw_map())
		return;

	std::lock_guard<std::mutex> lock(mtx);

	// arcdps does not always send a squad-combat-end in realtime; fall back to a quiet period
	if (cmb_state == combat_state::IN_BATTLE && GetTickCount() - last_cbt_evt > 15 * 1000)
		end_fight_locked();

	if (kte_settings.bHideInCombat && cmb_state == combat_state::IN_BATTLE)
		return;

	bool made_title_invis = false;
	if (!kte_settings.bTitleBg)
	{
		ImVec4 color = ImGui::GetStyleColorVec4(ImGuiCol_TitleBg);
		ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(color.x, color.y, color.z, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(color.x, color.y, color.z, 0.0f));
		made_title_invis = true;
	}
	ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(100, 10));

	if (ImGui::GetStyle().ScrollbarSize == 0)
		kte_settings.wFlags |= ImGuiWindowFlags_NoScrollbar;

	if (reset_pos)
	{
		ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 4, ImGui::GetIO().DisplaySize.y / 2));
		ImGui::SetNextWindowSize(ImVec2(200, 100));
		reset_pos = false;
	}
	if (ImGui::Begin("Know thy enemy", &kte_settings.bEnabled, kte_settings.wFlags))
	{
		switch (get_display_state())
		{
		case display_state::BATTLE:
			draw_battle_wait();
			break;
		case display_state::HISTORY_TAB:
			draw_teams_tabbed();
			break;
		case display_state::HISTORY_COL:
			draw_teams_columns();
			break;
		case display_state::NONE:
		default:
			ImGui::TextUnformatted("No teams");
			ImGui::Separator();
			ImGui::TextUnformatted("No hits");
			break;
		}

		if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight))
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 0));
			if (ImGui::BeginMenu("History"))
			{
				draw_history_menu();
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Style"))
			{
				draw_style_menu();
				ImGui::EndMenu();
			}
			ImGui::PopStyleVar();
			ImGui::EndPopup();
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();
	if (made_title_invis)
		ImGui::PopStyleColor(2);
}

/* ---------------------------------------------------------------------------
 * Settings
 * ------------------------------------------------------------------------- */
std::wstring settings_file_path()
{
	if (!get_settings_path)
		return L"";
	const wchar_t* ini = get_settings_path();
	if (!ini)
		return L"";
	std::wstring path(ini);
	path = path.substr(0, path.find_last_of(L"\\") + 1);
	path.append(L"know_thy_enemy_settings_v2.bin");
	return path;
}

void save_kte_settings()
{
	std::wstring path = settings_file_path();
	if (path.empty())
		return;
	std::ofstream file(path.c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
	if (file.good())
		file.write((const char*)&kte_settings, sizeof(settings));
}

void load_kte_settings()
{
	std::wstring path = settings_file_path();
	if (path.empty())
		return;
	std::ifstream file(path.c_str(), std::ios::in | std::ios::binary);
	settings temp;
	if (file.good() && file.read((char*)&temp, sizeof(settings)) && temp.magic == SETTINGS_MAGIC)
	{
		temp.cteam1.name[31] = temp.cteam2.name[31] = temp.cteam3.name[31] = 0;
		kte_settings = temp;
	}
}

/* ---------------------------------------------------------------------------
 * arcdps entry points
 * ------------------------------------------------------------------------- */
static void* imgui_malloc(size_t size, void*) { return arc_malloc ? arc_malloc(size) : malloc(size); }
static void imgui_free(void* ptr, void*) { if (arc_free) arc_free(ptr); else free(ptr); }

uintptr_t mod_release()
{
	save_kte_settings();
	if (mumble)
		UnmapViewOfFile(mumble);
	if (mumble_handle)
		CloseHandle(mumble_handle);
	mumble = nullptr;
	mumble_handle = nullptr;
	return 0;
}

static arcdps_exports arc_exports = {};
arcdps_exports* mod_init()
{
	memset(&arc_exports, 0, sizeof(arcdps_exports));
	arc_exports.sig = 0xC0FFEE;
	arc_exports.imguivers = IMGUI_VERSION_NUM;
	arc_exports.size = sizeof(arcdps_exports);
	arc_exports.out_name = "Know thy enemy";
	arc_exports.out_build = "5.0.0";
	arc_exports.imgui = imgui_proc;
	arc_exports.wnd_nofilter = mod_wnd;
	arc_exports.combat = mod_combat;
	arc_exports.options_end = options_end_proc;
	arc_exports.options_windows = options_windows_proc;
	if (arccolors)
		arccolors(color_array);
	load_kte_settings();
	log_arc("know_thy_enemy mod_init");
	return &arc_exports;
}

extern "C" __declspec(dllexport) void* get_release_addr()
{
	return (void*)mod_release;
}

extern "C" __declspec(dllexport) void* get_init_addr(const char* arcversion, ImGuiContext* imguictx, void* id3dptr, HMODULE arcdll_handle, void* mallocfn, void* freefn, uint32_t imguiversion)
{
	arcdll = arcdll_handle;
	arclog = (void (*)(const char*))GetProcAddress(arcdll, "e8");
	if (imguiversion != IMGUI_VERSION_NUM)
	{
		// sharing an ImGui context across versions crashes the game, refuse to load instead
		char buf[128];
		snprintf(buf, sizeof(buf), "know_thy_enemy: built for ImGui %d but arcdps uses %u, not loading", IMGUI_VERSION_NUM, imguiversion);
		log_arc(buf);
		return nullptr;
	}
	get_settings_path = (const wchar_t* (*)())GetProcAddress(arcdll, "e0");
	arccolors = (void (*)(ImVec4**))GetProcAddress(arcdll, "e5");
	get_ui_settings = (uint64_t (*)())GetProcAddress(arcdll, "e6");
	get_key_settings = (uint64_t (*)())GetProcAddress(arcdll, "e7");
	arc_malloc = (void* (*)(size_t))mallocfn;
	arc_free = (void (*)(void*))freefn;
	ImGui::SetCurrentContext(imguictx);
	ImGui::SetAllocatorFunctions(imgui_malloc, imgui_free);
	return (void*)mod_init;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved)
{
	return TRUE;
}
