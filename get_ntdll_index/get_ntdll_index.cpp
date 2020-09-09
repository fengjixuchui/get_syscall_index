﻿

#include <Windows.h>

BOOL safeWow64DisableDirectory(
	PVOID& arg
)
{
	typedef BOOL (WINAPI *fnWow64DisableWow64FsRedirection)(PVOID* OldValue);
	fnWow64DisableWow64FsRedirection pfnWow64DisableWow64FsRedirection = \
		(fnWow64DisableWow64FsRedirection) \
		GetProcAddress(GetModuleHandleW(L"kernel32"), "Wow64DisableWow64FsRedirection");
	if (pfnWow64DisableWow64FsRedirection) {
		pfnWow64DisableWow64FsRedirection(&arg);
		return TRUE;
	}
	else {
		return FALSE;
	}
}


BOOL safeWow64ReverDirectory(
	PVOID& arg
)
{
	typedef BOOL (WINAPI *fnWow64RevertWow64FsRedirection)(PVOID* OldValue);
	fnWow64RevertWow64FsRedirection pfnWow64RevertWow64FsRedirection = \
		(fnWow64RevertWow64FsRedirection) \
		GetProcAddress(GetModuleHandleW(L"kernel32"), "Wow64RevertWow64FsRedirection");
	if (pfnWow64RevertWow64FsRedirection) {
		pfnWow64RevertWow64FsRedirection(&arg);
		return TRUE;
	}
	else {
		return FALSE;
	}
}


VOID safeGetNativeSystemInfo(
	__out LPSYSTEM_INFO lpSystemInfo
)
{
	if (NULL == lpSystemInfo)	return;

	typedef VOID (WINAPI *fnGetNativeSystemInfo)(LPSYSTEM_INFO lpSystemInfo);
	fnGetNativeSystemInfo pfnGetNativeSystemInfo = \
		(fnGetNativeSystemInfo) \
		GetProcAddress(GetModuleHandleW(L"kernel32"), "GetNativeSystemInfo");
	if (pfnGetNativeSystemInfo)
		pfnGetNativeSystemInfo(lpSystemInfo);
	else
		GetSystemInfo(lpSystemInfo);
}


VOID safeGetVersion(
	__out PRTL_OSVERSIONINFOW lpVersionInformation
)
{
	if (NULL == lpVersionInformation)	return;

	typedef NTSTATUS(WINAPI* fnRtlGetVersion)(PRTL_OSVERSIONINFOW lpVersionInformation);
	fnRtlGetVersion pfnRtlGetVersion = \
		(fnRtlGetVersion) \
		GetProcAddress(GetModuleHandleW(L"ntdll"), "RtlGetVersion");
	if (pfnRtlGetVersion)
		pfnRtlGetVersion(lpVersionInformation);
	else
#pragma warning(push)
#pragma warning(disable : 4996)
		if (FALSE == GetVersionExW(lpVersionInformation)) lpVersionInformation = { };
#pragma warning(pop)
}


BOOL WINAPI isWindows7OrGreater() {
	OSVERSIONINFOW osVer = { };
	OSVERSIONINFOW osCheck = { };
	osCheck.dwMajorVersion = HIBYTE(_WIN32_WINNT_WIN7);
	osCheck.dwMinorVersion = LOBYTE(_WIN32_WINNT_WIN7);
	safeGetVersion(&osVer);
	return (osVer.dwPlatformId == VER_PLATFORM_WIN32_NT &&
		osVer.dwMajorVersion > osCheck.dwMajorVersion ||
		((osVer.dwMajorVersion == osCheck.dwMajorVersion) && 
			(osVer.dwMinorVersion >= osCheck.dwMinorVersion)));
}


BOOL WINAPI isOs64() {
	SYSTEM_INFO si = { };
	safeGetNativeSystemInfo(&si);
	return (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ||
		si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64);
}


BOOL WINAPI isWow64(HANDLE hProcess = ::GetCurrentProcess()) {
	BOOL bWow64 = FALSE;
	return (isOs64() && IsWow64Process(hProcess, &bWow64) && bWow64);
}



#include <peconv.h>
#include <unordered_map>

bool parse_function_syscall_index(void * fn, uint32_t & u32Idx, bool b64bit = isOs64()) {
	if (NULL == fn)
		return false;

	PBYTE pFn = PBYTE(fn);

	if (b64bit) {
		if (0xB8 == *((uint8_t*)(pFn + 3))) {
			uint32_t syscall_index = *((uint32_t*)(pFn + 4));
			u32Idx = syscall_index;
			return true;
		}
	}
	else {
		if (0xB8 == *((uint8_t*)(pFn + 0))) {
			uint32_t syscall_index = *((uint32_t*)(pFn + 1));
			u32Idx = syscall_index;
			return true;
		}
	}

	return false;
}


bool get_syscall_tables(std::unordered_map<std::string, uint32_t> & syscall_tables) {

	syscall_tables.clear();

#ifndef _WIN64
	PVOID wow64FsReDirectory = NULL;
	BOOL isWow64FsReDriectory = isWow64();

	if (isWow64FsReDriectory)
		safeWow64DisableDirectory(wow64FsReDirectory);
#endif

	CHAR sysNtDll[MAX_PATH] = { };
	ExpandEnvironmentStringsA("%systemroot%\\system32\\ntdll.dll", sysNtDll, sizeof(sysNtDll));

	size_t v_size = 0;
	// Load the current executable from the file with the help of libpeconv:
	PBYTE loaded_pe = peconv::load_pe_module(sysNtDll, v_size, true, true);
	if (!loaded_pe) {
		return false;
	}

	std::vector<std::string> name_list;
	peconv::get_exported_names(loaded_pe, name_list);
	for (std::string name : name_list) {
		if ('N' == name[0] && 't' == name[1] 
			|| 'Z' == name[0] && 'w' == name[1])
		{
			FARPROC fn = peconv::get_exported_func(loaded_pe, (LPSTR)name.c_str());
			uint32_t u32Idx = 0;
			if (parse_function_syscall_index(fn, u32Idx, peconv::is64bit(loaded_pe)))
				syscall_tables[name.c_str()] = u32Idx;
		}
	}
	peconv::free_pe_buffer(loaded_pe);

#ifndef  _WIN64
	if (isWow64FsReDriectory)
		safeWow64ReverDirectory(wow64FsReDirectory);
#endif

	return bool(syscall_tables.size());
}






#include <iostream>

int main()
{
	if (!isWindows7OrGreater())
		printf_s("sorry, the system is not supported.\n");
	
	std::unordered_map<std::string, uint32_t> syscall_tables;
	bool is_ok = get_syscall_tables(syscall_tables);

	printf_s("get sys call tables: %hs\n", is_ok ? "succeed." : "failed.");

	for (auto item : syscall_tables)
		printf_s("name:\t%hs  [%x]\n", item.first.c_str(), item.second);

	::system("pause");
	return EXIT_SUCCESS;
}

