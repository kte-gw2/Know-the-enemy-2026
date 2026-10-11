/* Subset of the public arcdps extension API (https://www.deltaconnected.com/arcdps/api/) */
#pragma once
#include <cstdint>
#include <windows.h>

enum iff : uint8_t {
	IFF_FRIEND,
	IFF_FOE,
	IFF_UNKNOWN
};

enum cbtstatechange : uint8_t {
	CBTS_NONE = 0,
	CBTS_SQCOMBATSTART = 9, /* previously CBTS_LOGSTART */
	CBTS_SQCOMBATEND = 10,  /* previously CBTS_LOGEND */
};

/* combat event (revision 1) */
typedef struct cbtevent {
	uint64_t time;
	uintptr_t src_agent;
	uintptr_t dst_agent;
	int32_t value;
	int32_t buff_dmg;
	uint32_t overstack_value;
	uint32_t skillid;
	uint16_t src_instid;
	uint16_t dst_instid;
	uint16_t src_master_instid;
	uint16_t dst_master_instid;
	uint8_t iff;
	uint8_t buff;
	uint8_t result;
	uint8_t is_activation;
	uint8_t is_buffremove;
	uint8_t is_ninety;
	uint8_t is_fifty;
	uint8_t is_moving;
	uint8_t is_statechange;
	uint8_t is_flanking;
	uint8_t is_shields;
	uint8_t is_offcycle;
	uint8_t pad61;
	uint8_t pad62;
	uint8_t pad63;
	uint8_t pad64;
} cbtevent;

/* agent short */
typedef struct ag {
	const char* name; /* agent name. may be null. valid only at time of event. utf8 */
	uintptr_t id;     /* agent unique identifier */
	uint32_t prof;    /* profession at time of event */
	uint32_t elite;   /* elite spec at time of event, 0xFFFFFFFF for non-players */
	uint32_t self;    /* 1 if self, 0 if not */
	uint16_t team;    /* sep21+ */
} ag;

typedef struct arcdps_exports {
	uintptr_t size;
	uint32_t sig;
	uint32_t imguivers;
	const char* out_name;
	const char* out_build;
	uintptr_t (*wnd_nofilter)(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	void (*combat)(cbtevent* ev, ag* src, ag* dst, const char* skillname, uint64_t id, uint64_t revision);
	void (*imgui)(uint32_t not_charsel_or_loading, uint32_t hide_if_combat_or_ooc);
	void (*options_end)();
	void (*combat_local)(cbtevent* ev, ag* src, ag* dst, const char* skillname, uint64_t id, uint64_t revision);
	uintptr_t (*wnd_filter)(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	int (*options_windows)(const char* windowname);
} arcdps_exports;

static_assert(sizeof(cbtevent) == 64, "cbtevent size");
static_assert(sizeof(arcdps_exports) == 88, "arcdps_exports size");
