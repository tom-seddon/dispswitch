//#define _HAS_EXCEPTIONS 0
#define STRICT
#define WIN32_LEAN_AND_MEAN
#pragma warning(push)
#pragma warning(disable:4389)//warning C4389: '==' : signed/unsigned mismatch
#pragma warning(disable:4018)//warning C4018: <operator> : signed/unsigned mismatch
#pragma warning(disable:4702)//warning C4702: unreachable code
#include <windows.h>
#include <TlHelp32.h>
#include <shellapi.h>
#include <vector>
#include <algorithm>
#include <stdio.h>
#include <stdarg.h>
#include "TRexpp.h"
#include <shlwapi.h>
#pragma warning(pop)

extern const bool g_false = false;

#ifdef _DEBUG
#define ENABLE_OUTPUTDEBUGSTRING
#endif

#ifdef ENABLE_OUTPUTDEBUGSTRING
#define ODS(X) OutputDebugString(X)
#else//ENABLE_OUTPUTDEBUGSTRING
#define ODS(X)
#endif//ENABLE_OUTPUTDEBUGSTRING

#define GTC (void(0))

#ifndef GTC

#define GTC\
	do\
				{\
		tick=GetTickCount()-tick;\
		ODS(strprintf("%s(%d): tick=%lu\n",__FILE__,__LINE__,tick));\
				}\
										while(g_false)

#endif

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static const char *strprintf(const char *fmt, ...)
{
	static char s_buf[1024];

	va_list v;
	va_start(v, fmt);
	_vsnprintf(s_buf, sizeof s_buf - 1, fmt, v);
	va_end(v);

	return s_buf;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

struct MonitorInfo
{
	HMONITOR hmonitor;
	size_t original_index;
	MONITORINFOEX info;
};

static BOOL CALLBACK GetAllMonitorsMonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData)
{
	(void)hdcMonitor, (void)lprcMonitor;

	auto monitors = (std::vector<MonitorInfo> *)dwData;

	MonitorInfo mi;

	mi.hmonitor = hMonitor;

	mi.original_index = monitors->size();

	mi.info.cbSize = sizeof mi.info;
	GetMonitorInfo(mi.hmonitor, &mi.info);

	monitors->emplace_back(mi);

	return TRUE;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void EnumProcesses(std::vector<PROCESSENTRY32> *processes)
{
	processes->clear();

	// Find file name of program that owns the window.
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
		return;

	PROCESSENTRY32 pe;
	pe.dwSize = sizeof pe;

	if (Process32First(snapshot, &pe))
	{
		do
		{
			if (pe.dwSize == sizeof pe)
				processes->push_back(pe);

			pe.dwSize = sizeof pe;
		} while (Process32Next(snapshot, &pe));

		CloseHandle(snapshot);
	}
}

static void FindExeFileByProcessId(char *exe_file, DWORD process_id, const std::vector<PROCESSENTRY32> &processes)
{
	strcpy(exe_file, "");

	for (unsigned i = 0; i < processes.size(); ++i)
	{
		const PROCESSENTRY32 *pe = &processes[i];

		if (pe->th32ProcessID == process_id)
		{
			strcpy(exe_file, pe->szExeFile);
			break;
		}
	}
}

struct WindowDetails
{
	unsigned flags;
	DWORD process_id;
	char exe_file[MAX_PATH];
};

static void GetWindowDetails(const std::vector<PROCESSENTRY32> &processes, HWND hwnd, WindowDetails *details)
{
	GetWindowThreadProcessId(hwnd, &details->process_id);

	FindExeFileByProcessId(details->exe_file, details->process_id,processes);

	char exe_name[MAX_PATH];
	strcpy(exe_name, details->exe_file);
	PathStripPath(exe_name);

	int n = GetWindowTextLength(hwnd);
	std::vector<char> title(n + 1);
	GetWindowText(hwnd, &title[0], title.size());

	details->flags = 0;

	// Add flags based on window caps
	if ((GetWindowLong(hwnd, GWL_STYLE)&WS_THICKFRAME) == 0)
		details->flags |= F_DONT_RESIZE;

	// Add flags from INI
	for (const FlagsName *f = g_flags_names; f->name; ++f)
		AddFlag(&details->flags, hwnd, &title[0], exe_name, f->name, f->flags);
}

static void Help(const wchar_t *bad_arg)
{
	std::wstring msg;

	msg = L"Bad flag \"";
	msg += bad_arg;
	msg += L"\" on command line.\n\nValid flags are:\n\n";

	for (const FlagsName *f = g_flags_names; f->name; ++f)
	{
		msg += f->wname;
		msg += L"\n";
	}

	MessageBoxW(0, msg.c_str(), L"dispswitch error", MB_OK | MB_ICONERROR);
}

static BOOL CALLBACK GetAllVisibleWindowsWindowEnumProc(HWND hwnd, LPARAM lParam)
{
	std::vector<HWND> *hwnds = reinterpret_cast<std::vector<HWND> *>(lParam);
	if (IsWindowVisible(hwnd))
		hwnds->push_back(hwnd);

	return TRUE;
}

static LONG GetRectWidth(const RECT &r)
{
	return r.right - r.left;
}

static LONG GetRectHeight(const RECT &r)
{
	return r.bottom - r.top;
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	DWORD tick = GetTickCount();
	(void)tick;

	unsigned cmd_line_flags = 0;
	int argc;
	LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	for (int i = 1; i < argc; ++i)
	{
		const FlagsName *f;
		for (f = g_flags_names; f->name; ++f)
		{
			if (_wcsicmp(argv[i], f->wname) == 0)
			{
				cmd_line_flags |= f->flags;
				break;
			}
		}

		if (!f->name)
		{
			Help(argv[i]);
			return 1;
		}
	}

	GTC;

	char exe[MAX_PATH];
	GetModuleFileName(GetModuleHandle(0), exe, sizeof exe);
	PathRemoveFileSpec(exe);
	PathCombine(g_ini_path, exe, g_ini_file_name);
	ODS(strprintf("dispswitch: %s: INI path is \"%s\".\n", __FUNCTION__, g_ini_path));

	// 	STARTUPINFO si;
	// 	GetStartupInfo(&si);

	GTC;

	std::vector<MonitorInfo> monitors;
	EnumDisplayMonitors(0, 0, &GetAllMonitorsMonitorEnumProc, LPARAM(&monitors));
	if (monitors.size() <= 1)
		return 0;

	std::sort(monitors.begin(), monitors.end(), [](const MonitorInfo &a, const MonitorInfo &b) { return a.info.rcWork.left < b.info.rcWork.left; });

	for (unsigned i = 0; i < monitors.size(); ++i)
	{
		const MonitorInfo *mi = &monitors[i];
		(void)mi;

		ODS(strprintf("dispswitch: monitor %u/%u:\n", 1 + i, monitors.size()));
		ODS(strprintf("    Original index: %u\n", (unsigned)mi->original_index));
		ODS(strprintf("    Device=\"%s\"\n", mi->info.szDevice));
		ODS(strprintf("    MonitorRect=(%ld,%ld)-(%ld,%ld)\n",
			mi->info.rcMonitor.left, mi->info.rcMonitor.top, mi->info.rcMonitor.right, mi->info.rcMonitor.bottom));
		ODS(strprintf("    WorkRect=(%ld,%ld)-(%ld,%ld)\n",
			mi->info.rcWork.left, mi->info.rcWork.top, mi->info.rcWork.right, mi->info.rcWork.bottom));
	}

	//
	std::vector<PROCESSENTRY32> processes;
	EnumProcesses(&processes);

	GTC;

	// 
	HWND fg = GetForegroundWindow();
	WindowDetails fg_details;
	GetWindowDetails(processes, fg, &fg_details);

	ODS(strprintf("exe=%s\n", fg_details.exe_file));

	std::vector<HWND> hwnds;
	if (fg_details.flags&F_TAKE_EXE)
	{
		EnumWindows(&GetAllVisibleWindowsWindowEnumProc, LPARAM(&hwnds));
		ODS(strprintf("%u hwnds.\n", hwnds.size()));
		std::vector<HWND>::iterator hwnd = hwnds.begin();
		while (hwnd != hwnds.end())
		{
			char exe_file[MAX_PATH];
			GetExeFileForWindow(processes, *hwnd, exe_file);
			if (strcmp(exe_file, fg_details.exe_file) == 0)
				++hwnd;
			else
				hwnd = hwnds.erase(hwnd);
		}
	}
	else
	{
		HWND h = fg;
		do
		{
			hwnds.push_back(h);
			h = GetParent(h);
		} while (h && (fg_details.flags&F_TAKE_PARENT));
	}

	GTC;

	ODS(strprintf("%u hwnds.\n", hwnds.size()));

	for (unsigned i = 0; i < hwnds.size(); ++i)
	{
		HWND fg_wnd = hwnds[i];

		bool was_zoomed = false;
		if (IsZoomed(fg_wnd))
		{
			was_zoomed = true;
			ShowWindow(fg_wnd, SW_RESTORE);
		}

		RECT fg_rect;
		GetWindowRect(fg_wnd, &fg_rect);

		ODS(strprintf("dispswitch: old rect=(%ld,%ld)->(%ld,%ld).\n", fg_rect.left, fg_rect.top, fg_rect.right,
			fg_rect.bottom));

		HMONITOR fg_monitor = MonitorFromRect(&fg_rect, MONITOR_DEFAULTTOPRIMARY);

		size_t monitor_idx;
		for (monitor_idx = 0; monitor_idx < monitors.size(); ++monitor_idx)
		{
			if (monitors[monitor_idx].hmonitor == fg_monitor)
				break;
		}

		if (monitor_idx >= monitors.size())
		{
			// Oh dear.
			continue;
		}

		const MONITORINFOEX *old_info = &monitors[monitor_idx].info;

		++monitor_idx;
		monitor_idx %= monitors.size();

		const MONITORINFOEX *new_info = &monitors[monitor_idx].info;

		//
		RECT dr;
		dr.left = fg_rect.left - old_info->rcWork.left;
		dr.top = fg_rect.top - old_info->rcWork.top;
		dr.right = fg_rect.right - old_info->rcWork.right;
		dr.bottom = fg_rect.bottom - old_info->rcWork.bottom;

		if (fg_details.flags&F_DONT_RESIZE)
		{
			LONG cx = (fg_rect.left + fg_rect.right) / 2 - old_info->rcWork.left;
			LONG cy = (fg_rect.top + fg_rect.bottom) / 2 - old_info->rcWork.top;

			float tcx = cx / (float)GetRectWidth(old_info->rcWork);
			float tcy = cy / (float)GetRectHeight(old_info->rcWork);

			LONG fg_w = GetRectWidth(fg_rect);
			LONG fg_h = GetRectHeight(fg_rect);

			fg_rect.left = new_info->rcWork.left + LONG(tcx*GetRectWidth(new_info->rcWork) + .5f) - fg_w / 2;
			fg_rect.top = new_info->rcWork.top + LONG(tcy*GetRectHeight(new_info->rcWork) + .5f) - fg_h / 2;

			fg_rect.right = fg_rect.left + fg_w;
			fg_rect.bottom = fg_rect.top + fg_h;

			// don't let it go outside the work rect.
			LONG dx = new_info->rcWork.left - fg_rect.left;
			if (dx < 0)
				dx = 0;

			LONG dy = new_info->rcWork.top - fg_rect.top;
			if (dy < 0)
				dy = 0;

			OffsetRect(&fg_rect, dx, dy);
		}
		else
		{
			float left_frac, top_frac, right_frac, bottom_frac;
			{
				RECT local_rect = fg_rect;
				OffsetRect(&local_rect, -old_info->rcWork.left, -old_info->rcWork.top);

				left_frac = local_rect.left / float(GetRectWidth(old_info->rcWork));
				top_frac = local_rect.top / float(GetRectHeight(old_info->rcWork));
				right_frac = local_rect.right / float(GetRectWidth(old_info->rcWork));
				bottom_frac = local_rect.bottom / float(GetRectHeight(old_info->rcWork));
			}

			if (fg_details.flags&F_SIDE_RELATIVE)
			{
				fg_rect.left = new_info->rcWork.left + dr.left;
				fg_rect.top = new_info->rcWork.top + dr.top;
				fg_rect.right = new_info->rcWork.right + dr.right;
				fg_rect.bottom = new_info->rcWork.bottom + dr.bottom;
			}
			else
			{
				// round to nearest instead of round to zero, prevents window
				// floating towards the origin with repeated invocations (or at
				// least it did with the test window).
				fg_rect.left = new_info->rcWork.left + LONG(left_frac*GetRectWidth(new_info->rcWork) + .5f);
				fg_rect.top = new_info->rcWork.top + LONG(top_frac*GetRectHeight(new_info->rcWork) + .5f);
				fg_rect.right = new_info->rcWork.left + LONG(right_frac*GetRectWidth(new_info->rcWork) + .5f);
				fg_rect.bottom = new_info->rcWork.top + LONG(bottom_frac*GetRectHeight(new_info->rcWork) + .5f);
			}

			// Work out a minimum size for this window.
			LONG min_width, min_height;
			{
				// Include a small client area
				RECT rect = { 0, 0, 100, 100 };
				DWORD style = GetWindowLong(fg_wnd, GWL_STYLE);
				DWORD ex_style = GetWindowLong(fg_wnd, GWL_EXSTYLE);
				BOOL menu = !!GetMenu(fg_wnd);
				AdjustWindowRectEx(&rect, style, menu, ex_style);

				min_width = GetRectWidth(rect);
				min_height = GetRectHeight(rect);

				ODS(strprintf("dispswitch: min size=%ldx%ld\n", min_width, min_height));
			}

			// Clamp size, rather crudely.
			if (GetRectWidth(fg_rect) < min_width)
				fg_rect.right = fg_rect.left + min_width;

			if (GetRectHeight(fg_rect) < min_height)
				fg_rect.bottom = fg_rect.top + min_height;
		}

		ODS(strprintf("dispswitch: new rect=(%ld,%ld)->(%ld,%ld).\n", fg_rect.left, fg_rect.top, fg_rect.right,
			fg_rect.bottom));

		// 		BOOL ok=MoveWindow(fg_wnd,fg_rect.left,fg_rect.top,fg_rect.right-fg_rect.left,
		// 			fg_rect.bottom-fg_rect.top,TRUE);
		BOOL ok = SetWindowPos(fg_wnd, 0, fg_rect.left, fg_rect.top, fg_rect.right - fg_rect.left,
			fg_rect.bottom - fg_rect.top, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOCOPYBITS);
		if (!ok)
		{
			DWORD err = GetLastError();
			(void)err;
			ODS(strprintf("dispswitch:     move failed. Error: 0x%08lX\n", err));
		}

		if (was_zoomed)
			ShowWindow(fg_wnd, SW_MAXIMIZE);
	}

	return 0;
}
