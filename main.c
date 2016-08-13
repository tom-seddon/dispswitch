#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>
#include <stdio.h>
#include <ShellScalingAPI.h>
#include <ctype.h>

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void dprintf(const char *fmt, ...) {
	char tmp[10000];

	va_list v;
	va_start(v, fmt);
	_vsnprintf(tmp, sizeof tmp, fmt, v);
	tmp[sizeof tmp - 1] = 0;
	va_end(v);

	OutputDebugStringA(tmp);
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

#define RW(X) ((X).right-(X).left)
#define RH(X) ((X).bottom-(X).top)

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

// dbgview prints each OutputDebugStringA call separately, hence this crap.
#define NUM_RECT_BUFS (4)
#define RECT_BUF_SIZE (100)
static char g_rect_bufs[NUM_RECT_BUFS][RECT_BUF_SIZE];
static size_t g_next_rect_buf;

static const char *GetRectString(const RECT *r) {
	char *buf = g_rect_bufs[g_next_rect_buf++];
	g_next_rect_buf %= NUM_RECT_BUFS;

	snprintf(buf, RECT_BUF_SIZE, "(%ld,%ld)-(%ld,%ld) (%ldx%ld)", r->left, r->top, r->right, r->bottom, RW(*r), RH(*r));
	return buf;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static const char *GetDpiAwarenessString(DPI_AWARENESS a) {
	switch (a) {
	default:
		return "?";

	case DPI_AWARENESS_INVALID:
		return "INVALID";

	case 	DPI_AWARENESS_UNAWARE:
		return "UNAWARE";

	case 	DPI_AWARENESS_SYSTEM_AWARE:
		return "SYSTEM_AWARE";

	case 	DPI_AWARENESS_PER_MONITOR_AWARE:
		return "PER_MONITOR_AWARE";
	}
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void Fail(DWORD error, const char *fmt, ...)
{
	char err[1000];
	if (error == 0)
		snprintf(err, sizeof err, "no error");
	else {
		FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, error, 0, err, sizeof err, NULL);

		while (strlen(err) > 0 && isspace(err[strlen(err) - 1]))
			err[strlen(err) - 1] = 0;
	}

	char msg[1000];
	{
		va_list v;
		va_start(v, fmt);
		_vsnprintf(msg, sizeof msg, fmt, v);
		msg[sizeof msg - 1] = 0;
		va_end(v);
	}

	char tmp[5000];
	snprintf(tmp, sizeof tmp, "%s\n(%s)", msg, err);

	MessageBoxA(NULL, tmp, "dispswitch error", MB_ICONERROR | MB_OK);

	exit(1);
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

struct Monitor {
	HMONITOR h;
	MONITORINFOEX i;
};
typedef struct Monitor Monitor;

#define MAX_NUM_MONITORS (100) // srsly
static Monitor g_monitors[MAX_NUM_MONITORS];
static size_t g_num_monitors;

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static BOOL CALLBACK GetAllMonitorsMonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
	(void)hdcMonitor, (void)lprcMonitor, (void)dwData;

	if (g_num_monitors < MAX_NUM_MONITORS) {
		Monitor *m = &g_monitors[g_num_monitors++];

		m->h = hMonitor;

		m->i.cbSize = sizeof m->i;
		GetMonitorInfo(m->h, (MONITORINFO *)&m->i);
	}

	return TRUE;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static const Monitor *FindMonitorByHandle(HMONITOR h) {
	for (size_t i = 0; i < g_num_monitors; ++i) {
		const Monitor *m = &g_monitors[i];

		if (m->h == h)
			return m;
	}

	return NULL;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static int CompareMonitorsByLeftOfWorkRect(const void *a_, const void *b_) {
	const Monitor *a = a_, *b = b_;

	if (a->i.rcWork.left < b->i.rcWork.left)
		return -1;
	else if (b->i.rcWork.left < a->i.rcWork.left)
		return 1;
	else
		return 0;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
	(void)hInstance, (void)hPrevInstance, (void)lpCmdLine, (void)nCmdShow;

	SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

	if (!EnumDisplayMonitors(NULL, NULL, &GetAllMonitorsMonitorEnumProc, 0) || g_num_monitors <= 1)
		Fail(GetLastError(), "Monitor enumeration failed");

	qsort(g_monitors, g_num_monitors, sizeof g_monitors[0], &CompareMonitorsByLeftOfWorkRect);

	for (size_t i = 0; i < g_num_monitors; ++i) {
		const Monitor *m = &g_monitors[i];

		dprintf("Monitor %zu:\n", i);
		dprintf("    Work Rect: %s\n", GetRectString(&m->i.rcWork));
		dprintf("    Monitor Rect: %s\n", GetRectString(&m->i.rcMonitor));
	}

	HWND fg = GetForegroundWindow();
	if (!fg)
		Fail(GetLastError(), "No foreground window");

	DPI_AWARENESS dpi_awareness;
	{
		DPI_AWARENESS_CONTEXT context = GetWindowDpiAwarenessContext(fg);
		if (!context)
			Fail(GetLastError(), "Failed to get DPI awareness context");

		dpi_awareness = GetAwarenessFromDpiAwarenessContext(context);
		if (dpi_awareness == DPI_AWARENESS_INVALID)
			Fail(0, "Failed to get DPI awareness");
	}
	//GetProcessDpiAwareness(

	BOOL was_zoomed = FALSE;
	if (IsZoomed(fg))
	{
		ShowWindow(fg, 1);// First call's command is ignored, apparently...
		ShowWindow(fg, SW_RESTORE);

		was_zoomed = TRUE;
	}

	//bool can_resize = !!(GetWindowLong(h, GWL_STYLE)&WS_THICKFRAME);

	RECT old_rect;
	GetWindowRect(fg, &old_rect);

	HMONITOR old_hm = MonitorFromRect(&old_rect, MONITOR_DEFAULTTONEAREST);
	const Monitor *om = FindMonitorByHandle(old_hm);
	if (!om)
		return 0;

	const Monitor *nm = om + 1;
	if (nm == g_monitors + g_num_monitors)
		nm = g_monitors;

	double left_frac, top_frac, right_frac, bottom_frac;
	{
		RECT local = old_rect;
		OffsetRect(&local, -om->i.rcWork.left, -om->i.rcWork.top);

		double ow = RW(om->i.rcWork);
		double oh = RH(om->i.rcWork);

		left_frac = local.left / ow;
		top_frac = local.top / oh;
		right_frac = local.right / ow;
		bottom_frac = local.bottom / oh;
	}

	RECT new_rect;
	{
		double nw = RW(nm->i.rcWork);
		double nh = RH(nm->i.rcWork);

		new_rect.left = (LONG)(left_frac*nw + .5);
		new_rect.top = (LONG)(top_frac*nh + .5);
		new_rect.right = (LONG)(right_frac*nw + .5);
		new_rect.bottom = (LONG)(bottom_frac*nh + .5);

		OffsetRect(&new_rect, nm->i.rcWork.left, nm->i.rcWork.top);
	}

	{
		// Include a small client area
		RECT min_rect = { 0,0,100,100 };

		DWORD style = GetWindowLong(fg, GWL_STYLE);
		DWORD exstyle = GetWindowLong(fg, GWL_EXSTYLE);
		BOOL menu = !!GetMenu(fg);
		AdjustWindowRectEx(&min_rect, style, menu, exstyle);

		LONG minw = RW(min_rect);
		LONG minh = RH(min_rect);

		if (RW(new_rect) < minw)
			new_rect.right = new_rect.left + minw;

		if (RH(new_rect) < minh)
			new_rect.bottom = new_rect.top + minh;
	}

	UINT oxdpi, oydpi;
	GetDpiForMonitor(om->h, MDT_EFFECTIVE_DPI, &oxdpi, &oydpi);

	UINT nxdpi, nydpi;
	GetDpiForMonitor(nm->h, MDT_EFFECTIVE_DPI, &nxdpi, &nydpi);

	dprintf("Process DPI awareness: %s\n", GetDpiAwarenessString(dpi_awareness));
	dprintf("Old: rect: %s; rcWork: %s; DPI (%u,%u)\n", GetRectString(&old_rect), GetRectString(&om->i.rcWork), oxdpi, oydpi);
	dprintf("New: rect: %s; rcWork: %s; DPI (%u,%u)\n", GetRectString(&new_rect), GetRectString(&nm->i.rcWork), nxdpi, nydpi);

	PhysicalToLogicalPointForPerMonitorDPI(fg, (POINT *)&new_rect.left);
	PhysicalToLogicalPointForPerMonitorDPI(fg, (POINT *)&new_rect.right);

	dprintf("Adjusted new rect: %s\n", GetRectString(&new_rect));

	//	DWORD flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_

	DWORD flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOCOPYBITS;

	if (dpi_awareness == PROCESS_PER_MONITOR_DPI_AWARE && (oxdpi != nxdpi || oydpi != nydpi)) {
		// When moving and resizing a window with SetWindowPos results in a DPI
		// change, the new size gets adjusted, apparently under the assumption
		// it was being measured in the original display's coordinates. No good
		// here, because the different resolution has already been taken into
		// account! 
		//
		// To avoid this, first move the window to the new monitor without
		// resizing. This gives the program a chance to do its WM_DPICHANGE
		// thing and settle down. Then move it to its final position.
		//
		// (It's probably possible to foil this with windows that straddle
		// monitors; the logic probably uses MonitorFromRect, which this code
		// doesn't.)
		SetWindowPos(fg, NULL, new_rect.left, new_rect.top, -1, -1, flags | SWP_NOSIZE);
	}

	SetWindowPos(fg, NULL, new_rect.left, new_rect.top, RW(new_rect), RH(new_rect), flags);

	if (was_zoomed)
		ShowWindow(fg, SW_MAXIMIZE);

	return 0;
}