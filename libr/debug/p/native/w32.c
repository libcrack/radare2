#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <winbase.h>
#include <psapi.h>

#include "maps/windows.c"

static HANDLE tid2handler(int pid, int tid);

// XXX remove
#define WIN32_PI(x) x
#if 0
// list windows.. required to get list of windows for current pid and send kill signals
BOOL CALLBACK enumWindowsProc (HWND hwnd, LPARAM lParam) {

DWORD procid;

GetWindowThreadProcessId (hwnd, &procid);

if ((HANDLE)procid == g_hProc) { staticchar module[1024];
module[0] = 0;

if (g_softPhoneTitle.Size() > 0) { int rc = GetWindowText (hwnd, module, 1023);
module[rc] = 0;

}

if (IsWindow(hwnd) && ((g_appTitle.Size() == 0) || (g_appTitle.EqualsNoCase(module)))) {
g_hWnd = hwnd;

return (false);
}

}

return (true);
}

int

findApplicationWindow (void) {
g_hWnd = NULL;

EnumWindows (enumWindowsProc, 0);

return (0);
}
#endif

#if 0

1860 typedef struct _FLOATING_SAVE_AREA {
1861         DWORD   ControlWord;
1862         DWORD   StatusWord;
1863         DWORD   TagWord;
1864         DWORD   ErrorOffset;

1865         DWORD   ErrorSelector;
1866         DWORD   DataOffset;
1867         DWORD   DataSelector;
1868         BYTE    RegArea[80];
1869         DWORD   Cr0NpxState;
1870 } FLOATING_SAVE_AREA;

1871 typedef struct _CONTEXT {
1872         DWORD   ContextFlags;
1873         DWORD   Dr0;
1874         DWORD   Dr1;
1875         DWORD   Dr2;
1876         DWORD   Dr3;
1877         DWORD   Dr6;
1878         DWORD   Dr7;
1879         FLOATING_SAVE_AREA FloatSave;
1880         DWORD   SegGs;
1881         DWORD   SegFs;
1882         DWORD   SegEs;
1883         DWORD   SegDs;
1884         DWORD   Edi;
1885         DWORD   Esi;
1886         DWORD   Ebx;
1887         DWORD   Edx;
1888         DWORD   Ecx;
1889         DWORD   Eax;
1890         DWORD   Ebp;
1891         DWORD   Eip;
1892         DWORD   SegCs;
1893         DWORD   EFlags;
1894         DWORD   Esp;
1895         DWORD   SegSs;
1896         BYTE    ExtendedRegs[MAXIMUM_SUPPORTED_EXTENSION];
1897 } CONTEXT;
#endif

//BOOL WINAPI DebugActiveProcessStop(DWORD dwProcessId);

BOOL WINAPI DebugBreakProcess(
  HANDLE Process
  //_In_  HANDLE Process
);
static void (*gmbn)(HANDLE, HMODULE, LPTSTR, int) = NULL;
static int (*gmi)(HANDLE, HMODULE, LPMODULEINFO, int) = NULL;
static BOOL WINAPI (*w32_detach)(DWORD) = NULL;
static HANDLE WINAPI (*w32_openthread)(DWORD, BOOL, DWORD) = NULL;
static BOOL WINAPI (*w32_dbgbreak)(HANDLE) = NULL;
static DWORD WINAPI (*w32_getthreadid)(HANDLE) = NULL; // Vista
static DWORD WINAPI (*w32_getprocessid)(HANDLE) = NULL; // XP
static HANDLE WINAPI (*w32_openprocess)(DWORD, BOOL, DWORD) = NULL;


static void r_str_wtoc(char* d, const WCHAR* s) {
	int i = 0;
	while (s[i] != '\0') {
		d[i] = (char)s[i];
		++i;
	}
	d[i] = 0;
}

static int w32dbg_SeDebugPrivilege() {
	/////////////////////////////////////////////////////////
	//   Note: Enabling SeDebugPrivilege adapted from sample
	//     MSDN @ http://msdn.microsoft.com/en-us/library/aa446619%28VS.85%29.aspx
	// Enable SeDebugPrivilege
	int ret = R_TRUE;
	TOKEN_PRIVILEGES tokenPriv;
	HANDLE hToken = NULL;
	LUID luidDebug;
	if (!OpenProcessToken (GetCurrentProcess (),
			TOKEN_ADJUST_PRIVILEGES, &hToken))
		return R_FALSE;
		
	if (!LookupPrivilegeValue (NULL, SE_DEBUG_NAME, &luidDebug)) {
		CloseHandle (hToken);
		return R_FALSE;
	}

	tokenPriv.PrivilegeCount = 1;
	tokenPriv.Privileges[0].Luid = luidDebug;
	tokenPriv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	if (AdjustTokenPrivileges (hToken, FALSE, &tokenPriv, 0, NULL, NULL) != FALSE) {
		if (tokenPriv.Privileges[0].Attributes == SE_PRIVILEGE_ENABLED) {
		//	eprintf ("PRIV ENABLED\n");
		}
		// Always successful, even in the cases which lead to OpenProcess failure
		//	eprintf ("Successfully changed token privileges.\n");
		// XXX if we cant get the token nobody tells?? wtf
	} else {
		eprintf ("Failed to change token privileges 0x%x\n", (int)GetLastError());
		ret = R_FALSE;
	}
	CloseHandle (hToken);
	return ret;
}

static void print_lasterr(const char *str) {
	/* code from MSDN, :? */
	LPWSTR pMessage = L"%1!*.*s! %4 %5!*s!";
	DWORD_PTR pArgs[] = { (DWORD_PTR)4, (DWORD_PTR)2, (DWORD_PTR)L"Bill",  // %1!*.*s!
		(DWORD_PTR)L"Bob",                                                // %4
		(DWORD_PTR)6, (DWORD_PTR)L"Bill" };                               // %5!*s!
	WCHAR buffer[200];
	char cbuffer[100];
	if (!FormatMessage (FORMAT_MESSAGE_FROM_STRING |
				FORMAT_MESSAGE_ARGUMENT_ARRAY,
				pMessage,
				0,  // ignored
				0,  // ignored
				(LPTSTR)&buffer,
				sizeof (buffer)-1,
				(va_list*)pArgs)) {
		eprintf ("(%s): Format message failed with 0x%x\n",
			r_str_get (str), (ut32)GetLastError ());
		return;
	}
	r_str_wtoc (cbuffer, buffer);
	eprintf ("print_lasterr: %s ::: %s\n", r_str_get (str), r_str_get (cbuffer));
}


static int w32_dbg_init() {
	HANDLE lib;

	/* escalate privs (required for win7/vista) */
	w32dbg_SeDebugPrivilege ();
	/* lookup function pointers for portability */
	w32_detach = (BOOL WINAPI (*)(DWORD))
		GetProcAddress (GetModuleHandle ("kernel32"),
				"DebugActiveProcessStop");
	w32_openthread = (HANDLE WINAPI (*)(DWORD, BOOL, DWORD))
		GetProcAddress (GetModuleHandle ("kernel32"), "OpenThread");
	w32_openprocess = (HANDLE WINAPI (*)(DWORD, BOOL, DWORD))
		GetProcAddress (GetModuleHandle ("kernel32"), "OpenProcess");
	w32_dbgbreak = (BOOL WINAPI (*)(HANDLE))
		GetProcAddress (GetModuleHandle ("kernel32"),
				"DebugBreakProcess");
	// only windows vista :(
	w32_getthreadid = (DWORD WINAPI (*)(HANDLE))
		GetProcAddress (GetModuleHandle ("kernel32"), "GetThreadId");
	// from xp1
	w32_getprocessid = (DWORD WINAPI (*)(HANDLE))  
		GetProcAddress (GetModuleHandle ("kernel32"), "GetProcessId");

	lib = LoadLibrary ("psapi.dll");
	if(lib == NULL) {
		eprintf ("Cannot load psapi.dll!!\n");
		return R_FALSE;
	}
	gmbn = (void (*)(HANDLE, HMODULE, LPTSTR, int))
		GetProcAddress (lib, "GetModuleBaseNameA");
	gmi = (int (*)(HANDLE, HMODULE, LPMODULEINFO, int))
		GetProcAddress (lib, "GetModuleInformation");
	if (w32_detach == NULL || w32_openthread == NULL || w32_dbgbreak == NULL || 
	   gmbn == NULL || gmi == NULL) {
		// OOPS!
		eprintf ("debug_init_calls:\n"
			"DebugActiveProcessStop: 0x%p\n"
			"OpenThread: 0x%p\n"
			"DebugBreakProcess: 0x%p\n"
			"GetThreadId: 0x%p\n",
			w32_detach, w32_openthread, w32_dbgbreak, w32_getthreadid);
		return R_FALSE;
	}
	return R_TRUE;
}

#if 0
static HANDLE w32_t2h(pid_t tid) {
	TH_INFO *th = get_th (tid);
	if(th == NULL) {
		/* refresh thread list */
		w32_dbg_threads (tid);

		/* try to search thread */
		if((th = get_th (tid)) == NULL)
			return NULL;
	}
	return th->ht;
}
#endif

inline static int w32_h2t(HANDLE h) {
	if (w32_getthreadid != NULL) // >= Windows Vista
		return w32_getthreadid (h);
	if (w32_getprocessid != NULL) // >= Windows XP1
		return w32_getprocessid (h);
	return (int)(size_t)h; // XXX broken
}

static inline int w32_h2p(HANDLE h) {
	return w32_getprocessid (h);
}

// TODO: not yet used !!!
static int w32_first_thread(int pid) {
        HANDLE th; 
        HANDLE thid; 
        THREADENTRY32 te32;
        int ret = -1;

        te32.dwSize = sizeof(THREADENTRY32);

	if (w32_openthread == NULL) {
		eprintf("w32_thread_list: no w32_openthread?\n");
		return -1;
	}
        th = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, pid); 
        if (th == INVALID_HANDLE_VALUE) {
		eprintf ("w32_thread_list: invalid handle\n");
		return -1;
	}
	if (!Thread32First (th, &te32)) {
                CloseHandle (th);
		eprintf ("w32_thread_list: no thread first\n");
		return -1;
	}
        do {
                /* get all threads of process */
                if (te32.th32OwnerProcessID == pid) {
			thid = w32_openthread (THREAD_ALL_ACCESS, 0, te32.th32ThreadID);
			if (thid == NULL)
                                goto err_load_th;
			CloseHandle (th);
			return te32.th32ThreadID;
		}
        } while (Thread32Next (th, &te32));
err_load_th:    
        if (ret == -1) 
                print_lasterr ((char *)__FUNCTION__);
eprintf ("w32thread: Oops\n");
	return pid; // -1 ?
}

static int debug_exception_event (unsigned long code) {
	switch (code) {
	case EXCEPTION_BREAKPOINT:
		//eprintf ("breakpoint\n");
		break;
	case EXCEPTION_SINGLE_STEP:
		//eprintf ("singlestep\n");
		break;
	/* fatal exceptions */
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
	case EXCEPTION_STACK_OVERFLOW:
		eprintf ("fatal exception\n");
		break;
#if __MINGW64__
	case 0x4000001f: //STATUS_WX86_BREAKPOINT
		eprintf("WOW64 Loaded.\n");
		return 1;
		break;
#endif
	default:
		eprintf ("unknown exception\n");
		break;
	}
	return 0;
}

static int w32_dbg_wait(RDebug *dbg, int pid) {
	DEBUG_EVENT de;
	int tid, next_event = 0;
	unsigned int code;
	int ret = R_DBG_REASON_UNKNOWN;

	do {
		/* handle debug events */
		if (WaitForDebugEvent (&de, INFINITE) == 0) {
			print_lasterr ((char *)__FUNCTION__);
			return -1;
		}
		/* save thread id */
		tid = de.dwThreadId;
		//pid = de.dwProcessId;
		dbg->tid=tid;
		/* get exception code */
		code = de.dwDebugEventCode;
		//eprintf("code: %x pid=%08x tid=%08x\n",code,pid,tid);
		/* Ctrl-C? */
		/* get kind of event */
		switch (code) {
		case CREATE_PROCESS_DEBUG_EVENT:
			eprintf ("(%d) created process (%d:%p)\n", pid, w32_h2t (de.u.CreateProcessInfo.hProcess), de.u.CreateProcessInfo.lpStartAddress);
			r_debug_native_continue (dbg, pid, tid, -1);
			next_event = 1;
			ret = R_DBG_REASON_NEW_PID;
			break;
		case EXIT_PROCESS_DEBUG_EVENT:
			eprintf ("\n\n______________[ process finished ]_______________\n\n");
			//debug_load();
			next_event = 0;
			ret = R_DBG_REASON_EXIT_PID;
			break;
		case CREATE_THREAD_DEBUG_EVENT:
			eprintf ("(%d) created thread (%p)\n", pid, de.u.CreateThread.lpStartAddress);
			r_debug_native_continue (dbg, pid, tid, -1);
			ret = R_DBG_REASON_NEW_TID;
			next_event = 1;
			break;
		case EXIT_THREAD_DEBUG_EVENT:
			eprintf ("EXIT_THREAD\n");
			r_debug_native_continue (dbg, pid, tid, -1);
			next_event = 1;
			ret = R_DBG_REASON_EXIT_TID;
			break;
		case LOAD_DLL_DEBUG_EVENT:
			eprintf ("(%d) Loading %s library at %p\n",pid, "", de.u.LoadDll.lpBaseOfDll);
			r_debug_native_continue (dbg, pid, tid, -1);
			next_event = 1;
			ret = R_DBG_REASON_NEW_LIB;
			break;
		case UNLOAD_DLL_DEBUG_EVENT:
			eprintf ("UNLOAD_DLL\n");
			r_debug_native_continue (dbg, pid, tid, -1);
			next_event = 1;
			ret = R_DBG_REASON_EXIT_LIB;
			break;
		case OUTPUT_DEBUG_STRING_EVENT:
			eprintf("OUTPUT_DEBUG_STRING\n");
			r_debug_native_continue (dbg, pid, tid, -1);
			next_event = 1;
			break;
		case RIP_EVENT:
			eprintf("RIP_EVENT\n");
			r_debug_native_continue (dbg, pid, tid, -1);
			next_event = 1;
			// XXX unknown ret = R_DBG_REASON_TRAP;
			break;
		case EXCEPTION_DEBUG_EVENT:
			next_event = debug_exception_event (de.u.Exception.ExceptionRecord.ExceptionCode);
			if (!next_event)
				return R_DBG_REASON_TRAP;
			else 
				r_debug_native_continue (dbg, pid, tid, -1);
			break;
		default:
			eprintf ("Unknown event: %d\n", code);
			return -1;
		}
	} while (next_event);

	return ret;
}

static inline int CheckValidPE(unsigned char * PeHeader) {
	IMAGE_DOS_HEADER *dos_header = (IMAGE_DOS_HEADER *)PeHeader;
	IMAGE_NT_HEADERS *nt_headers;

	if (dos_header->e_magic==IMAGE_DOS_SIGNATURE) {
		nt_headers = (IMAGE_NT_HEADERS *)((char *)dos_header
				+ dos_header->e_lfanew);
		if (nt_headers->Signature==IMAGE_NT_SIGNATURE)
			return 1;
	}
	return 0;
}

static HANDLE tid2handler(int pid, int tid) {
        HANDLE th = CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, pid);
        THREADENTRY32 te32 = { .dwSize = sizeof (THREADENTRY32) };
        int ret = -1;
        if (th == INVALID_HANDLE_VALUE)
		return NULL;
	if (!Thread32First (th, &te32)) {
		CloseHandle (th);
                return NULL;
	}
        do {
                if (te32.th32OwnerProcessID == pid && te32.th32ThreadID == tid) {
			CloseHandle (th);
			return w32_openthread (THREAD_ALL_ACCESS, 0,
					te32.th32ThreadID);
		}
		ret++;
        } while (Thread32Next (th, &te32));
        if (ret == -1)
                print_lasterr ((char *)__FUNCTION__);
	CloseHandle (th);
        return NULL;
}

RList *w32_thread_list (int pid, RList *list) {
        HANDLE th; 
        HANDLE thid; 
        THREADENTRY32 te32;
        int ret;

        ret = -1; 
        te32.dwSize = sizeof(THREADENTRY32);

	if (w32_openthread == NULL) {
		eprintf("w32_thread_list: no w32_openthread?\n");
		return list;
	}
        th = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, pid); 
        if(th == INVALID_HANDLE_VALUE || !Thread32First (th, &te32))
                goto err_load_th;
        do {
                /* get all threads of process */
                if (te32.th32OwnerProcessID == pid) {
			//te32.dwFlags);
                        /* open a new handler */
			// XXX: fd leak?
#if 0
 75 typedef struct tagTHREADENTRY32 {
 76         DWORD dwSize;
 77         DWORD cntUsage;
 78         DWORD th32ThreadID;
 79         DWORD th32OwnerProcessID;
 80         LONG tpBasePri;
 81         LONG tpDeltaPri;
 82         DWORD dwFlags;
#endif
			thid = w32_openthread (THREAD_ALL_ACCESS, 0, te32.th32ThreadID);
			if (thid == NULL)
                                goto err_load_th;
                        ret = te32.th32ThreadID;
			//eprintf("Thread: %x %x\n", thid, te32.th32ThreadID);
			r_list_append (list, r_debug_pid_new ("???", te32.th32ThreadID, 's', 0));
                }
        } while (Thread32Next (th, &te32));
err_load_th:    
        if(ret == -1) 
                print_lasterr ((char *)__FUNCTION__);
        if(th != INVALID_HANDLE_VALUE)
                CloseHandle (th);
	return list;
}

// XXX hacky
RList *w32_pids (int pid, RList *list) {
        HANDLE th; 
        THREADENTRY32 te32;
        int ret = -1; 
        te32.dwSize = sizeof (THREADENTRY32);
	if (w32_openthread == NULL) {
		eprintf ("w32_thread_list: no w32_openthread?\n");
		return list;
	}
        th = CreateToolhelp32Snapshot (TH32CS_SNAPTHREAD, pid); 
        if(th == INVALID_HANDLE_VALUE || !Thread32First (th, &te32))
                goto err_load_th;
        do {
		if (ret != te32.th32OwnerProcessID)
			r_list_append (list, r_debug_pid_new ("???", te32.th32OwnerProcessID, 's', 0));
		ret = te32.th32OwnerProcessID;
        } while (Thread32Next (th, &te32));
err_load_th:    
        if(ret == -1) 
                print_lasterr ((char *)__FUNCTION__);
        if(th != INVALID_HANDLE_VALUE)
                CloseHandle (th);
	return list;
}
