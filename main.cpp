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

extern const bool g_false=false;

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


static char g_ini_file_name[]="dispswitch.ini";
static char g_ini_path[MAX_PATH];

static const char *strprintf(const char *fmt,...)
{
	static char s_buf[1024];

	va_list v;
	va_start(v,fmt);
	_vsnprintf(s_buf,sizeof s_buf-1,fmt,v);
	va_end(v);

	return s_buf;
}

static BOOL CALLBACK GetAllMonitorsMonitorEnumProc(HMONITOR hMonitor,HDC hdcMonitor,LPRECT lprcMonitor,
	LPARAM dwData)
{
	lprcMonitor,hdcMonitor;

	std::vector<HMONITOR> *monitors=reinterpret_cast<std::vector<HMONITOR> *>(dwData);
	monitors->push_back(hMonitor);
	return TRUE;
}

enum Flags
{
	F_DONT_RESIZE=1,
	F_TAKE_PARENT=2,
	F_TAKE_EXE=4,
	F_SIDE_RELATIVE=8,
};

struct FlagsName
{
	const char *name;//saves on converting everything to ...W
	const wchar_t *wname;
	unsigned flags;
};

#define NAME(X) X,L##X

static const FlagsName g_flags_names[]=
{
	{NAME("DontResize"),F_DONT_RESIZE},
	{NAME("TakeParent"),F_TAKE_PARENT},
	{NAME("TakeExe"),F_TAKE_EXE},
	{NAME("SideRelative"),F_SIDE_RELATIVE},
	{0},
};

static void AddFlag(unsigned *flags,HWND hwnd,const char *wnd_title,const char *exe_name,
	const char *flag_name,unsigned flag)
{
	hwnd;

	ODS(strprintf("dispswitch: %s: hwnd=0x%p wnd_title=\"%s\" exe_name=\"%s\" flag_name=\"%s\" flag=%u\n",
		__FUNCTION__,hwnd,wnd_title,exe_name,flag_name,flag));

	// error checking for this is really annoying!
	static const char s_default_text[]="\x2";//just some default string that
                                             //will never(tm) appear
	char re_text[1000];
	GetPrivateProfileString(exe_name,flag_name,s_default_text,re_text,
		sizeof re_text-1,g_ini_path);

	if(strcmp(re_text,s_default_text)==0)
	{
		ODS(strprintf("dispswitch:     no entry.\n"));
		return;
	}

	if(strlen(re_text)==0)
	{
		ODS(strprintf("dispswitch:     no regexp.\n"));
		
		// then set flag anyway, so it's easy to specify
	}
	else
	{
		ODS(strprintf("dispswitch:     regexp is \"%s\".\n",re_text));

		const char *re_err;
		TRex *re=trex_compile(re_text,&re_err);

		if(!re)
		{
			MessageBox(0,strprintf("Error in regexp \"%s\", for flag %s of %s.\n\n%s",re_text,flag_name,exe_name,re_err),
				"Regexp error",MB_OK|MB_ICONERROR);
			return;
		}

		TRexBool match=trex_match(re,wnd_title);

		trex_free(re);
		re=0;

		if(!match)
		{
			ODS(strprintf("dispswitch:     no match.\n"));
			return;
		}
	}

	*flags|=flag;
}

static void EnumProcesses(std::vector<PROCESSENTRY32> *processes)
{
	processes->clear();

	// Find file name of program that owns the window.
	HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
	if(snapshot==INVALID_HANDLE_VALUE)
		return;

	PROCESSENTRY32 pe;
	pe.dwSize=sizeof pe;

	if(Process32First(snapshot,&pe))
	{
		do 
		{
			if(pe.dwSize==sizeof pe)
				processes->push_back(pe);

			pe.dwSize=sizeof pe;
		}
		while(Process32Next(snapshot,&pe));

		CloseHandle(snapshot);
	}
}

static void GetExeFileForWindow(const std::vector<PROCESSENTRY32> &processes,HWND hwnd,char *exe_file)
{
	DWORD procid;
	GetWindowThreadProcessId(hwnd,&procid);

	strcpy(exe_file,"");

	for(unsigned i=0;i<processes.size();++i)
	{
		const PROCESSENTRY32 *pe=&processes[i];

		if(pe->th32ProcessID==procid)
		{
			strcpy(exe_file,pe->szExeFile);
			break;
		}
	}
}

struct WindowDetails
{
	unsigned flags;
	char exe_file[MAX_PATH];
};

static void GetWindowDetails(const std::vector<PROCESSENTRY32> &processes,HWND hwnd,WindowDetails *details)
{
	GetExeFileForWindow(processes,hwnd,details->exe_file);

	char exe_name[MAX_PATH];
	strcpy(exe_name,details->exe_file);
	PathStripPath(exe_name);

	int n=GetWindowTextLength(hwnd);
	std::vector<char> title(n+1);
	GetWindowText(hwnd,&title[0],title.size());

	details->flags=0;

	// Add flags based on window caps
	if((GetWindowLong(hwnd,GWL_STYLE)&WS_THICKFRAME)==0)
		details->flags|=F_DONT_RESIZE;

	// Add flags from INI
	for(const FlagsName *f=g_flags_names;f->name;++f)
		AddFlag(&details->flags,hwnd,&title[0],exe_name,f->name,f->flags);
}

static void Help(const wchar_t *bad_arg)
{
	std::wstring msg;

	msg=L"Bad flag \"";
	msg+=bad_arg;
	msg+=L"\" on command line.\n\nValid flags are:\n\n";

	for(const FlagsName *f=g_flags_names;f->name;++f)
	{
		msg+=f->wname;
		msg+=L"\n";
	}

	MessageBoxW(0,msg.c_str(),L"dispswitch error",MB_OK|MB_ICONERROR);
}

static BOOL CALLBACK GetAllVisibleWindowsWindowEnumProc(HWND hwnd,LPARAM lParam)
{
	std::vector<HWND> *hwnds=reinterpret_cast<std::vector<HWND> *>(lParam);
	if(IsWindowVisible(hwnd))
		hwnds->push_back(hwnd);

	return TRUE;
}

static LONG GetRectWidth(const RECT &r)
{
	return r.right-r.left;
}

static LONG GetRectHeight(const RECT &r)
{
	return r.bottom-r.top;
}

int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int)
{
	DWORD tick=GetTickCount();
	(void)tick;

	unsigned cmd_line_flags=0;
	int argc;
	LPWSTR *argv=CommandLineToArgvW(GetCommandLineW(),&argc);
	for(int i=1;i<argc;++i)
	{
		const FlagsName *f;
		for(f=g_flags_names;f->name;++f)
		{
			if(_wcsicmp(argv[i],f->wname)==0)
			{
				cmd_line_flags|=f->flags;
				break;
			}
		}

		if(!f->name)
		{
			Help(argv[i]);
			return 1;
		}
	}

	GTC;

	char exe[MAX_PATH];
	GetModuleFileName(GetModuleHandle(0),exe,sizeof exe);
	PathRemoveFileSpec(exe);
	PathCombine(g_ini_path,exe,g_ini_file_name);
	ODS(strprintf("dispswitch: %s: INI path is \"%s\".\n",__FUNCTION__,g_ini_path));

// 	STARTUPINFO si;
// 	GetStartupInfo(&si);

	GTC;

	std::vector<HMONITOR> monitors;
	EnumDisplayMonitors(0,0,&GetAllMonitorsMonitorEnumProc,LPARAM(&monitors));
	if(monitors.size()<=1)
		return 0;

	for(unsigned i=0;i<monitors.size();++i)
	{
		MONITORINFOEX mi;
		mi.cbSize=sizeof mi;
		GetMonitorInfo(monitors[i],&mi);

		ODS(strprintf("dispswitch: monitor %u/%u:\n",1+i,monitors.size()));
		ODS(strprintf("    Device=\"%s\"\n",mi.szDevice));
		ODS(strprintf("    MonitorRect=(%ld,%ld)-(%ld,%ld)\n",mi.rcMonitor.left,
			mi.rcMonitor.top,mi.rcMonitor.right,mi.rcMonitor.bottom));
		ODS(strprintf("    WorkRect=(%ld,%ld)-(%ld,%ld)\n",mi.rcWork.left,
			mi.rcWork.top,mi.rcWork.right,mi.rcWork.bottom));
	}

	//
	std::vector<PROCESSENTRY32> processes;
	EnumProcesses(&processes);

	GTC;

	// 
	HWND fg=GetForegroundWindow();
	WindowDetails fg_details;
	GetWindowDetails(processes,fg,&fg_details);

	ODS(strprintf("exe=%s\n",fg_details.exe_file));

	std::vector<HWND> hwnds;
	if(fg_details.flags&F_TAKE_EXE)
	{
		EnumWindows(&GetAllVisibleWindowsWindowEnumProc,LPARAM(&hwnds));
		ODS(strprintf("%u hwnds.\n",hwnds.size()));
		std::vector<HWND>::iterator hwnd=hwnds.begin();
		while(hwnd!=hwnds.end())
		{
			char exe_file[MAX_PATH];
			GetExeFileForWindow(processes,*hwnd,exe_file);
			if(strcmp(exe_file,fg_details.exe_file)==0)
				++hwnd;
			else
				hwnd=hwnds.erase(hwnd);
		}
	}
	else
	{
		HWND h=fg;
		do
		{
			hwnds.push_back(h);
			h=GetParent(h);
		}
		while(h&&(fg_details.flags&F_TAKE_PARENT));
	}

	GTC;

	ODS(strprintf("%u hwnds.\n",hwnds.size()));

	for(unsigned i=0;i<hwnds.size();++i)
	{
		HWND fg_wnd=hwnds[i];

		bool was_zoomed=false;
		if(IsZoomed(fg_wnd))
		{
			was_zoomed=true;
			ShowWindow(fg_wnd,SW_RESTORE);
		}

		RECT fg_rect;
		GetWindowRect(fg_wnd,&fg_rect);

		ODS(strprintf("dispswitch: old rect=(%ld,%ld)->(%ld,%ld).\n",fg_rect.left,fg_rect.top,fg_rect.right,
			fg_rect.bottom));

		HMONITOR fg_monitor=MonitorFromRect(&fg_rect,MONITOR_DEFAULTTOPRIMARY);

		MONITORINFOEX fg_monitor_info;
		fg_monitor_info.cbSize=sizeof fg_monitor_info;
		GetMonitorInfo(fg_monitor,&fg_monitor_info);

		unsigned monitor_idx=std::find(monitors.begin(),monitors.end(),fg_monitor)-monitors.begin();
		++monitor_idx;
		monitor_idx%=monitors.size();

		MONITORINFOEX new_monitor_info;
		new_monitor_info.cbSize=sizeof new_monitor_info;
		GetMonitorInfo(monitors[monitor_idx],&new_monitor_info);

		//
		RECT dr;
		dr.left=fg_rect.left-fg_monitor_info.rcWork.left;
		dr.top=fg_rect.top-fg_monitor_info.rcWork.top;
		dr.right=fg_rect.right-fg_monitor_info.rcWork.right;
		dr.bottom=fg_rect.bottom-fg_monitor_info.rcWork.bottom;

		if(fg_details.flags&F_DONT_RESIZE)
		{
			LONG cx=(fg_rect.left+fg_rect.right)/2-fg_monitor_info.rcWork.left;
			LONG cy=(fg_rect.top+fg_rect.bottom)/2-fg_monitor_info.rcWork.top;

			float tcx=cx/(float)GetRectWidth(fg_monitor_info.rcWork);
			float tcy=cy/(float)GetRectHeight(fg_monitor_info.rcWork);

			LONG fg_w=GetRectWidth(fg_rect);
			LONG fg_h=GetRectHeight(fg_rect);

			fg_rect.left=new_monitor_info.rcWork.left+LONG(tcx*GetRectWidth(new_monitor_info.rcWork)+.5f)-fg_w/2;
			fg_rect.top=new_monitor_info.rcWork.top+LONG(tcy*GetRectHeight(new_monitor_info.rcWork)+.5f)-fg_h/2;

			fg_rect.right=fg_rect.left+fg_w;
			fg_rect.bottom=fg_rect.top+fg_h;

			// don't let it go outside the work rect.
			LONG dx=new_monitor_info.rcWork.left-fg_rect.left;
			if(dx<0)
				dx=0;

			LONG dy=new_monitor_info.rcWork.top-fg_rect.top;
			if(dy<0)
				dy=0;

			OffsetRect(&fg_rect,dx,dy);
		}
		else
		{
			float left_frac,top_frac,right_frac,bottom_frac;
			{
				RECT local_rect=fg_rect;
				OffsetRect(&local_rect,-fg_monitor_info.rcWork.left,-fg_monitor_info.rcWork.top);

				left_frac=local_rect.left/float(GetRectWidth(fg_monitor_info.rcWork));
				top_frac=local_rect.top/float(GetRectHeight(fg_monitor_info.rcWork));
				right_frac=local_rect.right/float(GetRectWidth(fg_monitor_info.rcWork));
				bottom_frac=local_rect.bottom/float(GetRectHeight(fg_monitor_info.rcWork));
			}

			if(fg_details.flags&F_SIDE_RELATIVE)
			{
				fg_rect.left=new_monitor_info.rcWork.left+dr.left;
				fg_rect.top=new_monitor_info.rcWork.top+dr.top;
				fg_rect.right=new_monitor_info.rcWork.right+dr.right;
				fg_rect.bottom=new_monitor_info.rcWork.bottom+dr.bottom;
			}
			else
			{
				// round to nearest instead of round to zero, prevents window
                // floating towards the origin with repeated invocations (or at
                // least it did with the test window).
				fg_rect.left=new_monitor_info.rcWork.left+LONG(left_frac*GetRectWidth(new_monitor_info.rcWork)+.5f);
				fg_rect.top=new_monitor_info.rcWork.top+LONG(top_frac*GetRectHeight(new_monitor_info.rcWork)+.5f);
				fg_rect.right=new_monitor_info.rcWork.left+LONG(right_frac*GetRectWidth(new_monitor_info.rcWork)+.5f);
				fg_rect.bottom=new_monitor_info.rcWork.top+LONG(bottom_frac*GetRectHeight(new_monitor_info.rcWork)+.5f);
			}

			// Work out a minimum size for this window.
			LONG min_width,min_height;
			{
				// Include a small client area
				RECT rect={0,0,100,100};
				DWORD style=GetWindowLong(fg_wnd,GWL_STYLE);
				DWORD ex_style=GetWindowLong(fg_wnd,GWL_EXSTYLE);
				BOOL menu=!!GetMenu(fg_wnd);
				AdjustWindowRectEx(&rect,style,menu,ex_style);

				min_width=GetRectWidth(rect);
				min_height=GetRectHeight(rect);

				ODS(strprintf("dispswitch: min size=%ldx%ld\n",min_width,min_height));
			}

			// Clamp size, rather crudely.
			if(GetRectWidth(fg_rect)<min_width)
				fg_rect.right=fg_rect.left+min_width;

			if(GetRectHeight(fg_rect)<min_height)
				fg_rect.bottom=fg_rect.top+min_height;
		}

		ODS(strprintf("dispswitch: new rect=(%ld,%ld)->(%ld,%ld).\n",fg_rect.left,fg_rect.top,fg_rect.right,
			fg_rect.bottom));

// 		BOOL ok=MoveWindow(fg_wnd,fg_rect.left,fg_rect.top,fg_rect.right-fg_rect.left,
// 			fg_rect.bottom-fg_rect.top,TRUE);
		BOOL ok=SetWindowPos(fg_wnd,0,fg_rect.left,fg_rect.top,fg_rect.right-fg_rect.left,
			fg_rect.bottom-fg_rect.top,SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOZORDER|SWP_NOCOPYBITS);
		if(!ok)
		{
			DWORD err=GetLastError();
			(void)err;
			ODS(strprintf("dispswitch:     move failed. Error: 0x%08lX\n",err));
		}

		if(was_zoomed)
			ShowWindow(fg_wnd,SW_MAXIMIZE);
	}

	return 0;
}
