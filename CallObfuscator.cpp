#include <vector>
#include "xPE/xPE.h"

struct ObfuscateByName
{
	LPCSTR szOldApi;
	LPCSTR szNewApi;
	PIMAGE_THUNK_DATA lpOThunk;
	DWORD dwFThunkRva;
};

struct ReplaceApi
{
	LPCSTR szOldApi;
	LPCSTR szNewApi;
};

INT main(INT argc, CHAR** argv){

    if (argc >= 4){

        	LPCSTR szInFileName = argv[1];
        	LPCSTR szOutFileName = argv[2];

		std::vector<ObfuscateByName> ObfuscateByNameArr;
		std::vector<ReplaceApi> ReplaceApiArr;

		do
		{
			argc--;
			LPCSTR szOldApi = argv[argc];
			LPCSTR szNewApi = strstr(szOldApi, ",");
			if (!szNewApi)
			{
				Utils::Printf::Fail("Invalid apis array");
				return FALSE;
			};
			*(PBYTE)szNewApi = 0;
			szNewApi += 1;
			
			BYTE bReplaceApiElement[sizeof(ReplaceApi)] = { 0 };
			ReplaceApi* lpReplaceApiElement = (ReplaceApi*)bReplaceApiElement;
			lpReplaceApiElement->szOldApi = szOldApi;
			lpReplaceApiElement->szNewApi = szNewApi;
			ReplaceApiArr.push_back(*lpReplaceApiElement);

		} while (argc != 3);

		BYTE bPeFileInfo[sizeof(RAW_FILE_INFO)] = { 0 };
		if (!Load::File::Load(
			szInFileName,
			(PRAW_FILE_INFO)bPeFileInfo,
			sizeof(bPeFileInfo)
		))
		{
			Utils::Printf::Fail("Cannot load this file %s", szInFileName);
			return FALSE;
		};
		PRAW_FILE_INFO lpPeFileInfo = (PRAW_FILE_INFO)bPeFileInfo;

		DWORD PeArch = x32;
		if (!Load::PE::GetArch(lpPeFileInfo, &PeArch))
		{
			Utils::Printf::Fail("Cannot get the archeticture");
			return FALSE;
		};

#if defined(_M_X64) || defined(__amd64__)
		if (PeArch == x32)
		{
			Utils::Printf::Fail("Use the x32 binary to handle this PE");
			return FALSE;
		};
#else
		if (PeArch == x64)
		{
			Utils::Printf::Fail("Use the x64 binary to handle this PE");
			return FALSE;
		};
#endif

		PIMAGE_NT_HEADERS lpNtHeader = NULL;
		if (!Load::PE::GetNtHeader(
			lpPeFileInfo,
			&lpNtHeader
		))
		{
			Utils::Printf::Fail("Unable to get the NT header from the PE");
			return FALSE;
		};

		if (!Utils::IsValidReadPtr(
			lpPeFileInfo->lpDataBuffer,
			lpNtHeader->OptionalHeader.SizeOfHeaders
		))
		{
			Utils::Printf::Fail("Invalid headers size");
			return FALSE;
		};

		DWORD dwImportBaseOffset = 0;
		DWORD dwImportSize = 0;
		if (!Load::PE::GetDirectoryInfo(
			lpNtHeader,
			IMAGE_DIRECTORY_ENTRY_IMPORT,
			&dwImportBaseOffset,
			&dwImportSize,
			FALSE
		))
		{
			Utils::Printf::Fail("Unable to get the IMAGE_DIRECTORY_ENTRY_IMPORT table information");
			return FALSE;
		};

        	PIMAGE_IMPORT_DESCRIPTOR lpImportData = (PIMAGE_IMPORT_DESCRIPTOR)((DWORD_PTR)lpPeFileInfo->lpDataBuffer + dwImportBaseOffset);
		while (lpImportData->Name != NULL)
		{
			if (!Utils::IsValidReadPtr(
				lpImportData,
				sizeof(IMAGE_IMPORT_DESCRIPTOR)
			))
			{
				Utils::Printf::Fail("Invalid import directory");
				return FALSE;
			};

			DWORD dwLibNameOffset = lpImportData->Name;
			if (!Utils::RvaToOffset(lpNtHeader, dwLibNameOffset, &dwLibNameOffset))
			{
				Utils::Printf::Fail("Unable to get the imported library name");
				return FALSE;
			};
			PCHAR szDllName = (PCHAR)((DWORD_PTR)lpPeFileInfo->lpDataBuffer + dwLibNameOffset);

			if (!Utils::IsValidReadPtr(
				szDllName,
				sizeof(".dll")
			))
			{
				Utils::Printf::Fail("Invalid library name");
				return FALSE;
			};
			
			DWORD dwOriginalThunk = lpImportData->OriginalFirstThunk;

			if (!dwOriginalThunk)
			{
				Utils::Printf::Fail("Invalid Import table");
				return FALSE;
			};
			
			if (!Utils::RvaToOffset(lpNtHeader, dwOriginalThunk, &dwOriginalThunk))
			{
				Utils::Printf::Fail("Unable to get the IMAGE_THUNK_DATA array entry");
				return FALSE;
			};
			PIMAGE_THUNK_DATA dwCThunk = (PIMAGE_THUNK_DATA)((DWORD_PTR)lpPeFileInfo->lpDataBuffer + dwOriginalThunk);
			DWORD dwFirstThunk = lpImportData->FirstThunk;

			while (dwCThunk->u1.AddressOfData)
			{
				if (!Utils::IsValidReadPtr(
					dwCThunk,
					sizeof(IMAGE_THUNK_DATA)
				))
				{
					Utils::Printf::Fail("Invalid import directory");
					return FALSE;
				};

				if (IMAGE_SNAP_BY_ORDINAL(dwCThunk->u1.Ordinal)) {

					WORD wOrdinal = IMAGE_ORDINAL(dwCThunk->u1.Ordinal);
					Utils::Printf::Info("An import by ordinal %d found at %s, this cannot be handled for now", wOrdinal, szDllName);
				}
				else
				{
					DWORD lpApiImportOffset = (DWORD)dwCThunk->u1.AddressOfData;
					if (!Utils::RvaToOffset(lpNtHeader, lpApiImportOffset, &lpApiImportOffset))
					{
						Utils::Printf::Fail("Unable to get the IMAGE_IMPORT_BY_NAME structure pointer");
						return FALSE;
					};
					PIMAGE_IMPORT_BY_NAME lpApiImport = (PIMAGE_IMPORT_BY_NAME)((DWORD_PTR)lpPeFileInfo->lpDataBuffer + lpApiImportOffset);

					if (!Utils::IsValidReadPtr(
						lpApiImport,
						sizeof(IMAGE_IMPORT_BY_NAME)
					))
					{
						Utils::Printf::Fail("Invalid import directory");
						return FALSE;
					};

					for (auto const& ReplaceApiElement : ReplaceApiArr) {
						if (!strcmp(ReplaceApiElement.szOldApi, (PCHAR)lpApiImport->Name))
						{
							BYTE bObfuscateByNameElement[sizeof(ObfuscateByName)] = { 0 };
							ObfuscateByName* lpObfuscateByNameElement = (ObfuscateByName*)bObfuscateByNameElement;
							lpObfuscateByNameElement->dwFThunkRva = dwFirstThunk;
							lpObfuscateByNameElement->szOldApi = ReplaceApiElement.szOldApi;
							lpObfuscateByNameElement->szNewApi = ReplaceApiElement.szNewApi;
							lpObfuscateByNameElement->lpOThunk = dwCThunk;
							ObfuscateByNameArr.push_back(*lpObfuscateByNameElement);
							
							PIMAGE_SECTION_HEADER lpHeaderSection = IMAGE_FIRST_SECTION(lpNtHeader);
							for (DWORD dwSecIndex = 0; dwSecIndex < lpNtHeader->FileHeader.NumberOfSections; dwSecIndex++) {
								if (dwFirstThunk >= lpHeaderSection[dwSecIndex].VirtualAddress && 
									dwFirstThunk < lpHeaderSection[dwSecIndex].VirtualAddress + lpHeaderSection[dwSecIndex].Misc.VirtualSize)
								{
									lpHeaderSection[dwSecIndex].Characteristics |= IMAGE_SCN_MEM_WRITE;
									break;
								};
							};
							break;
						};
					};
				};
				dwOriginalThunk += sizeof(IMAGE_THUNK_DATA);
				dwFirstThunk += sizeof(IMAGE_THUNK_DATA);
				dwCThunk = (PIMAGE_THUNK_DATA)((DWORD_PTR)lpPeFileInfo->lpDataBuffer + dwOriginalThunk);
			};

			lpImportData++;
		};
	    
		if (!ObfuscateByNameArr.size()) return TRUE;
		DWORD dwEntryOffset = lpNtHeader->OptionalHeader.AddressOfEntryPoint;
		if (!Utils::RvaToOffset(lpNtHeader, dwEntryOffset, &dwEntryOffset))
		{
			Utils::Printf::Fail("Unable to get the entry point offset");
			return FALSE;
		};
		LPVOID lpEntry = (LPVOID)((DWORD_PTR)lpPeFileInfo->lpDataBuffer + dwEntryOffset);

		BYTE bOriginalBytes[0x5] = { 0 };
		if (!Utils::SafeMemoryCopy(
			(LPVOID)bOriginalBytes,
			sizeof(bOriginalBytes),
			lpEntry,
			sizeof(bOriginalBytes)
		))
		{
			Utils::Printf::Fail("Unable to read the original 5 bytes from the entry point");
			return FALSE;
		};

		BYTE bShellcode[] =
		{
#if defined(_M_X64) || defined(__amd64__)
			/*
					0:  41 5f                   pop    r15
					2:  49 83 ef 05             sub    r15,0x5
					6:  48 83 ec 28             sub    rsp,0x28
					a:  6a 60                   push   0x60
					c:  58                      pop    rax
					d:  65 48 8b 10             mov    rdx,QWORD PTR gs:[rax]
					11: 48 8b 52 18             mov    rdx,QWORD PTR [rdx+0x18]
					15: 48 8b 52 10             mov    rdx,QWORD PTR [rdx+0x10]
					19: 52                      push   rdx
					1a: 5e                      pop    rsi
					1b: 48 ad                   lods   rax,QWORD PTR ds:[rsi]
					1d: 48 96                   xchg   rsi,rax
					1f: 48 ad                   lods   rax,QWORD PTR ds:[rsi]
					21: 48 8b 68 30             mov    rbp,QWORD PTR [rax+0x30]
					25: 8b 45 3c                mov    eax,DWORD PTR [rbp+0x3c]
					28: 8b 9c 05 88 00 00 00    mov    ebx,DWORD PTR [rbp+rax*1+0x88]
					2f: 48 01 eb                add    rbx,rbp
					32: 8b 73 20                mov    esi,DWORD PTR [rbx+0x20]
					35: 48 01 ee                add    rsi,rbp
					38: 48 31 c9                xor    rcx,rcx
					3b: 48 ff c1                inc    rcx
					3e: ad                      lods   eax,DWORD PTR ds:[rsi]
					3f: 48 01 e8                add    rax,rbp
					42: 81 38 47 65 74 50       cmp    DWORD PTR [rax],0x50746547
					48: 75 f1                   jne    0x3b
					4a: 81 78 04 72 6f 63 41    cmp    DWORD PTR [rax+0x4],0x41636f72
					51: 75 e8                   jne    0x3b
					53: 81 78 08 64 64 72 65    cmp    DWORD PTR [rax+0x8],0x65726464
					5a: 75 df                   jne    0x3b
					5c: 8b 73 24                mov    esi,DWORD PTR [rbx+0x24]
					5f: 48 01 ee                add    rsi,rbp
					62: 66 8b 0c 4e             mov    cx,WORD PTR [rsi+rcx*2]
					66: 48 ff c9                dec    rcx
					69: 8b 73 1c                mov    esi,DWORD PTR [rbx+0x1c]
					6c: 48 01 ee                add    rsi,rbp
					6f: 8b 3c 8e                mov    edi,DWORD PTR [rsi+rcx*4]
					72: 48 01 ef                add    rdi,rbp
					75: 52                      push   rdx
					76: 5e                      pop    rsi
					77: 48 8b 6e 30             mov    rbp,QWORD PTR [rsi+0x30]
					7b: 56                      push   rsi
					7c: 41 5e                   pop    r14
					7e: eb 4f                   jmp    0xcf
					80: 5b                      pop    rbx
					81: 48 31 d2                xor    rdx,rdx
					84: 38 13                   cmp    BYTE PTR [rbx],dl
					86: 74 33                   je     0xbb
					88: 48 8b 36                mov    rsi,QWORD PTR [rsi]
					8b: 48 8b 4e 30             mov    rcx,QWORD PTR [rsi+0x30]
					8f: 48 85 c9                test   rcx,rcx
					92: 74 3a                   je     0xce
					94: 53                      push   rbx
					95: 5a                      pop    rdx
					96: ff d7                   call   rdi
					98: 48 85 c0                test   rax,rax
					9b: 74 eb                   je     0x88
					9d: 48 31 d2                xor    rdx,rdx
					a0: 48 ff c3                inc    rbx
					a3: 38 13                   cmp    BYTE PTR [rbx],dl
					a5: 75 f9                   jne    0xa0
					a7: 48 ff c3                inc    rbx
					aa: 8b 13                   mov    edx,DWORD PTR [rbx]
					ac: 48 01 ea                add    rdx,rbp
					af: 48 89 02                mov    QWORD PTR [rdx],rax
					b2: 41 56                   push   r14
					b4: 5e                      pop    rsi
					b5: 48 83 c3 04             add    rbx,0x4
					b9: eb c6                   jmp    0x81
					bb: 48 83 c4 28             add    rsp,0x28
					bf: 41 c7 07 ff ff ff ff    mov    DWORD PTR [r15],<first_original_four_bytes>
					c6: 41 c6 47 04 ff          mov    BYTE PTR [r15+0x4],<fifth_original_byte>
					cb: 41 ff e7                jmp    r15
					ce: cc                      int3
					cf: e8 ac ff ff ff          call   0x80
			*/

			0x41, 0x5F, 0x49, 0x83, 0xEF, 0x05, 0x48, 0x83, 0xEC, 0x28, 0x6A, 0x60, 0x58, 0x65, 0x48, 0x8B, 0x10, 0x48, 0x8B, 0x52, 0x18,
			0x48, 0x8B, 0x52, 0x10, 0x52, 0x5E, 0x48, 0xAD, 0x48, 0x96, 0x48, 0xAD, 0x48, 0x8B, 0x68, 0x30, 0x8B, 0x45, 0x3C, 0x8B, 0x9C,
			0x05, 0x88, 0x00, 0x00, 0x00, 0x48, 0x01, 0xEB, 0x8B, 0x73, 0x20, 0x48, 0x01, 0xEE, 0x48, 0x31, 0xC9, 0x48, 0xFF, 0xC1, 0xAD,
			0x48, 0x01, 0xE8, 0x81, 0x38, 0x47, 0x65, 0x74, 0x50, 0x75, 0xF1, 0x81, 0x78, 0x04, 0x72, 0x6F, 0x63, 0x41, 0x75, 0xE8, 0x81,
			0x78, 0x08, 0x64, 0x64, 0x72, 0x65, 0x75, 0xDF, 0x8B, 0x73, 0x24, 0x48, 0x01, 0xEE, 0x66, 0x8B, 0x0C, 0x4E, 0x48, 0xFF, 0xC9,
			0x8B, 0x73, 0x1C, 0x48, 0x01, 0xEE, 0x8B, 0x3C, 0x8E, 0x48, 0x01, 0xEF, 0x52, 0x5E, 0x48, 0x8B, 0x6E, 0x30, 0x56, 0x41, 0x5E,
			0xEB, 0x4F, 0x5B, 0x48, 0x31, 0xD2, 0x38, 0x13, 0x74, 0x33, 0x48, 0x8B, 0x36, 0x48, 0x8B, 0x4E, 0x30, 0x48, 0x85, 0xC9, 0x74,
			0x3A, 0x53, 0x5A, 0xFF, 0xD7, 0x48, 0x85, 0xC0, 0x74, 0xEB, 0x48, 0x31, 0xD2, 0x48, 0xFF, 0xC3, 0x38, 0x13, 0x75, 0xF9, 0x48,
			0xFF, 0xC3, 0x8B, 0x13, 0x48, 0x01, 0xEA, 0x48, 0x89, 0x02, 0x41, 0x56, 0x5E, 0x48, 0x83, 0xC3, 0x04, 0xEB, 0xC6, 0x48, 0x83,
			0xC4, 0x28, 0x41, 0xC7, 0x07, 
			bOriginalBytes[0], bOriginalBytes[1],
			bOriginalBytes[2], bOriginalBytes[3],
			0x41, 0xC6, 0x47, 0x04, bOriginalBytes[4],
			0x41, 0xFF, 0xE7, 0xCC, 0xE8, 0xAC, 0xFF, 0xFF, 0xFF
#else
			/*
					0:  6a 30                   push   0x30
					2:  58                      pop    eax
					3:  64 8b 10                mov    edx,DWORD PTR fs:[eax]
					6:  8b 52 0c                mov    edx,DWORD PTR [edx+0xc]
					9:  8b 52 0c                mov    edx,DWORD PTR [edx+0xc]
					c:  89 d6                   mov    esi,edx
					e:  ad                      lods   eax,DWORD PTR ds:[esi]
					f:  96                      xchg   esi,eax
					10: ad                      lods   eax,DWORD PTR ds:[esi]
					11: 8b 68 18                mov    ebp,DWORD PTR [eax+0x18]
					14: 8b 45 3c                mov    eax,DWORD PTR [ebp+0x3c]
					17: 8b 5c 05 78             mov    ebx,DWORD PTR [ebp+eax*1+0x78]
					1b: 01 eb                   add    ebx,ebp
					1d: 8b 73 20                mov    esi,DWORD PTR [ebx+0x20]
					20: 01 ee                   add    esi,ebp
					22: 31 c9                   xor    ecx,ecx
					24: 41                      inc    ecx
					25: ad                      lods   eax,DWORD PTR ds:[esi]
					26: 01 e8                   add    eax,ebp
					28: 81 38 47 65 74 50       cmp    DWORD PTR [eax],0x50746547
					2e: 75 f4                   jne    0x24
					30: 81 78 04 72 6f 63 41    cmp    DWORD PTR [eax+0x4],0x41636f72
					37: 75 eb                   jne    0x24
					39: 81 78 08 64 64 72 65    cmp    DWORD PTR [eax+0x8],0x65726464
					40: 75 e2                   jne    0x24
					42: 8b 73 24                mov    esi,DWORD PTR [ebx+0x24]
					45: 01 ee                   add    esi,ebp
					47: 66 8b 0c 4e             mov    cx,WORD PTR [esi+ecx*2]
					4b: 49                      dec    ecx
					4c: 8b 73 1c                mov    esi,DWORD PTR [ebx+0x1c]
					4f: 01 ee                   add    esi,ebp
					51: 8b 3c 8e                mov    edi,DWORD PTR [esi+ecx*4]
					54: 01 ef                   add    edi,ebp
					56: 89 d6                   mov    esi,edx
					58: 8b 6e 18                mov    ebp,DWORD PTR [esi+0x18]
					5b: 56                      push   esi
					5c: eb 41                   jmp    0x9f
					5e: 5b                      pop    ebx
					5f: 31 d2                   xor    edx,edx
					61: 38 13                   cmp    BYTE PTR [ebx],dl
					63: 74 27                   je     0x8c
					65: 8b 36                   mov    esi,DWORD PTR [esi]
					67: 8b 4e 18                mov    ecx,DWORD PTR [esi+0x18]
					6a: 85 c9                   test   ecx,ecx
					6c: 74 30                   je     0x9e
					6e: 53                      push   ebx
					6f: 51                      push   ecx
					70: ff d7                   call   edi
					72: 85 c0                   test   eax,eax
					74: 74 ef                   je     0x65
					76: 31 d2                   xor    edx,edx
					78: 43                      inc    ebx
					79: 38 13                   cmp    BYTE PTR [ebx],dl
					7b: 75 fb                   jne    0x78
					7d: 43                      inc    ebx
					7e: 8b 13                   mov    edx,DWORD PTR [ebx]
					80: 01 ea                   add    edx,ebp
					82: 89 02                   mov    DWORD PTR [edx],eax
					84: 8b 34 24                mov    esi,DWORD PTR [esp]
					87: 83 c3 04                add    ebx,0x4
					8a: eb d3                   jmp    0x5f
					8c: 5e                      pop    esi
					8d: 5d                      pop    ebp
					8e: 83 ed 05                sub    ebp,0x5
					91: c7 45 00 ff ff ff ff    mov    DWORD PTR [ebp+0x0],<first_original_four_bytes>
					98: c6 45 04 ff             mov    BYTE PTR [ebp+0x4],<fifth_original_byte>
					9c: ff e5                   jmp    ebp
					9e: cc                      int3
					9f: e8 ba ff ff ff          call   0x5e
			*/

			0x6A, 0x30, 0x58, 0x64, 0x8B, 0x10, 0x8B, 0x52, 0x0C, 0x8B, 0x52, 0x0C, 0x89, 0xD6, 0xAD, 0x96, 0xAD, 0x8B, 0x68, 0x18, 0x8B,
			0x45, 0x3C, 0x8B, 0x5C, 0x05, 0x78, 0x01, 0xEB, 0x8B, 0x73, 0x20, 0x01, 0xEE, 0x31, 0xC9, 0x41, 0xAD, 0x01, 0xE8, 0x81, 0x38,
			0x47, 0x65, 0x74, 0x50, 0x75, 0xF4, 0x81, 0x78, 0x04, 0x72, 0x6F, 0x63, 0x41, 0x75, 0xEB, 0x81, 0x78, 0x08, 0x64, 0x64, 0x72,
			0x65, 0x75, 0xE2, 0x8B, 0x73, 0x24, 0x01, 0xEE, 0x66, 0x8B, 0x0C, 0x4E, 0x49, 0x8B, 0x73, 0x1C, 0x01, 0xEE, 0x8B, 0x3C, 0x8E,
			0x01, 0xEF, 0x89, 0xD6, 0x8B, 0x6E, 0x18, 0x56, 0xEB, 0x41, 0x5B, 0x31, 0xD2, 0x38, 0x13, 0x74, 0x27, 0x8B, 0x36, 0x8B, 0x4E,
			0x18, 0x85, 0xC9, 0x74, 0x30, 0x53, 0x51, 0xFF, 0xD7, 0x85, 0xC0, 0x74, 0xEF, 0x31, 0xD2, 0x43, 0x38, 0x13, 0x75, 0xFB, 0x43,
			0x8B, 0x13, 0x01, 0xEA, 0x89, 0x02, 0x8B, 0x34, 0x24, 0x83, 0xC3, 0x04, 0xEB, 0xD3, 0x5E, 0x5D, 0x83, 0xED, 0x05, 0xC7, 0x45,
			0x00,
			bOriginalBytes[0], bOriginalBytes[1],
			bOriginalBytes[2], bOriginalBytes[3],
			0xC6, 0x45, 0x04, bOriginalBytes[4],
			0xFF, 0xE5, 0xCC, 0xE8, 0xBA, 0xFF, 0xFF, 0xFF
#endif
		};
		DWORD dwPaddedShellcodeSize = sizeof(bShellcode);

		for (auto const& ObfuscateByNameElement : ObfuscateByNameArr)
		{
			dwPaddedShellcodeSize += (DWORD)strlen(ObfuscateByNameElement.szOldApi) + 1;
			dwPaddedShellcodeSize += (DWORD)sizeof(ObfuscateByNameElement.dwFThunkRva);
		};
		dwPaddedShellcodeSize++;

		DWORD dwNewApisArrayOffset = dwPaddedShellcodeSize;

		for (auto const& ObfuscateByNameElement : ObfuscateByNameArr)
		{
			if (strlen(ObfuscateByNameElement.szNewApi) > strlen(ObfuscateByNameElement.szOldApi))
			{
				dwPaddedShellcodeSize += (DWORD)sizeof(WORD);
				dwPaddedShellcodeSize += (DWORD)strlen(ObfuscateByNameElement.szNewApi) + 1;
			};
		};

		LPVOID lpShellCode = NULL;
		if (!(lpShellCode = VirtualAlloc(
			NULL,
			dwPaddedShellcodeSize,
			MEM_COMMIT | MEM_RESERVE,
			PAGE_READWRITE
		)))
		{
			Utils::Reportf::ApiError("VirtualAlloc", "Cannot allocate the size of %d, for the shellcode", dwPaddedShellcodeSize);
			return FALSE;
		};

		CopyMemory(
			lpShellCode,
			bShellcode,
			sizeof(bShellcode)
		);
	    
	    	if (lpNtHeader->FileHeader.PointerToSymbolTable)
		{
			PIMAGE_SYMBOL lpCOFFSymbol = (PIMAGE_SYMBOL)((DWORD_PTR)lpPeFileInfo->lpDataBuffer +
				lpNtHeader->FileHeader.PointerToSymbolTable);
			LPVOID lpCOFFString = (LPVOID)((DWORD_PTR)lpCOFFSymbol + lpNtHeader->FileHeader.NumberOfSymbols * sizeof(IMAGE_SYMBOL));

			if (!Utils::IsValidWritePtr(
				lpCOFFSymbol,
				*(PDWORD)lpCOFFString + lpNtHeader->FileHeader.NumberOfSymbols * sizeof(IMAGE_SYMBOL)
			))
			{
				Utils::Printf::Fail("Invalid COFF symbol table");
				return FALSE;
			};

			ZeroMemory(
				lpCOFFSymbol,
				*(PDWORD)lpCOFFString + lpNtHeader->FileHeader.NumberOfSymbols * sizeof(IMAGE_SYMBOL)
			);

			lpNtHeader->FileHeader.PointerToSymbolTable = 0;
			lpNtHeader->FileHeader.NumberOfSymbols = 0;
		};

		BOOL IsDebugExists = FALSE;
		if (!Load::PE::IsDirExists(lpNtHeader, IMAGE_DIRECTORY_ENTRY_DEBUG, &IsDebugExists))
		{
			Utils::Printf::Fail("Cannot check if IMAGE_DIRECTORY_ENTRY_DEBUG table exists");
			return FALSE;
		};
		if (IsDebugExists)
		{
			DWORD dwDebugBaseOffset = 0;
			DWORD dwDebugSize = 0;
			if (!Load::PE::GetDirectoryInfo(
				lpNtHeader,
				IMAGE_DIRECTORY_ENTRY_DEBUG,
				&dwDebugBaseOffset,
				&dwDebugSize,
				FALSE
			))
			{
				Utils::Printf::Fail("Unable to get the IMAGE_DIRECTORY_ENTRY_DEBUG table information");
				return FALSE;
			};
			PIMAGE_DEBUG_DIRECTORY lpDebugData = (PIMAGE_DEBUG_DIRECTORY)((DWORD_PTR)lpPeFileInfo->lpDataBuffer + dwDebugBaseOffset);

			if (!Utils::IsValidWritePtr(
				lpDebugData,
				sizeof(IMAGE_DEBUG_DIRECTORY)
			))
			{
				Utils::Printf::Fail("Invalid debug directory");
				return FALSE;
			};

			ZeroMemory(
				lpDebugData,
				sizeof(IMAGE_DEBUG_DIRECTORY)
			);

			GET_DIRECTORY_ENTRY(lpNtHeader, IMAGE_DIRECTORY_ENTRY_DEBUG) = 0;
			GET_DIRECTORY_SIZE(lpNtHeader, IMAGE_DIRECTORY_ENTRY_DEBUG) = 0;
		};

		DWORD dwShellcodeOffset = 0;
		DWORD dwRawSizeNeeded = 0;

		PIMAGE_SECTION_HEADER lpExecutableSection = NULL;
		PIMAGE_SECTION_HEADER lpHeaderSection = IMAGE_FIRST_SECTION(lpNtHeader);
		for (DWORD dwSecIndex = 0; dwSecIndex < lpNtHeader->FileHeader.NumberOfSections; dwSecIndex++) {
			if (!dwShellcodeOffset && (lpHeaderSection[dwSecIndex].Characteristics & IMAGE_SCN_MEM_EXECUTE))
			{
				lpExecutableSection = &lpHeaderSection[dwSecIndex];
				lpHeaderSection[dwSecIndex].Characteristics |= IMAGE_SCN_MEM_WRITE;
				DWORD dwRealVirtualSize = 0;
				if (dwSecIndex == lpNtHeader->FileHeader.NumberOfSections - 1)
				{
					dwRealVirtualSize = lpNtHeader->OptionalHeader.SizeOfImage -
						lpHeaderSection[dwSecIndex].VirtualAddress;
				}
				else
				{
					dwRealVirtualSize = lpHeaderSection[dwSecIndex + 1].VirtualAddress -
						lpHeaderSection[dwSecIndex].VirtualAddress;
				};
				if (dwRealVirtualSize - lpHeaderSection[dwSecIndex].Misc.VirtualSize < dwPaddedShellcodeSize)
				{
					if (dwSecIndex != lpNtHeader->FileHeader.NumberOfSections - 1) continue;
					while (dwRealVirtualSize - lpHeaderSection[dwSecIndex].Misc.VirtualSize < dwPaddedShellcodeSize)
					{
						lpNtHeader->OptionalHeader.SizeOfImage += lpNtHeader->OptionalHeader.SectionAlignment;
						dwRealVirtualSize = lpNtHeader->OptionalHeader.SizeOfImage -
							lpHeaderSection[dwSecIndex].VirtualAddress;
					};
				};
				
				dwShellcodeOffset = lpHeaderSection[dwSecIndex].PointerToRawData + lpHeaderSection[dwSecIndex].Misc.VirtualSize;
				if (lpHeaderSection[dwSecIndex].SizeOfRawData - lpHeaderSection[dwSecIndex].Misc.VirtualSize >= dwPaddedShellcodeSize)
				{
					lpHeaderSection[dwSecIndex].Misc.VirtualSize += dwPaddedShellcodeSize;
					dwPaddedShellcodeSize += lpHeaderSection[dwSecIndex].SizeOfRawData - lpHeaderSection[dwSecIndex].Misc.VirtualSize;
					dwRawSizeNeeded = 0;
					break;
				};

				lpHeaderSection[dwSecIndex].Misc.VirtualSize += dwPaddedShellcodeSize;

				DWORD dwVirtualRawSpace = 0;
				if ((dwVirtualRawSpace = Utils::AlignUp(
					dwShellcodeOffset,
					lpNtHeader->OptionalHeader.FileAlignment
				) - dwShellcodeOffset) < dwPaddedShellcodeSize)
				{
					dwRawSizeNeeded = Utils::AlignUp(
						dwPaddedShellcodeSize - dwVirtualRawSpace,
						lpNtHeader->OptionalHeader.FileAlignment
					);
				}
				else dwRawSizeNeeded = 0;
				dwPaddedShellcodeSize += dwRawSizeNeeded + lpHeaderSection[dwSecIndex].SizeOfRawData - lpHeaderSection[dwSecIndex].Misc.VirtualSize;

				BOOL IsSecurityExists = FALSE;
				if (!Load::PE::IsDirExists(lpNtHeader, IMAGE_DIRECTORY_ENTRY_SECURITY, &IsSecurityExists))
				{
					Utils::Printf::Fail("Cannot check if IMAGE_DIRECTORY_ENTRY_SECURITY table exists");
					return FALSE;
				};
				if (IsSecurityExists)
				{
					DWORD dwSecurityBaseOffset = 0;
					DWORD dwSecuritySize = 0;
					if (!Load::PE::GetDirectoryInfo(
						lpNtHeader,
						IMAGE_DIRECTORY_ENTRY_SECURITY,
						&dwSecurityBaseOffset,
						&dwSecuritySize,
						FALSE
					))
					{
						Utils::Printf::Fail("Unable to get the IMAGE_DIRECTORY_ENTRY_SECURITY table information");
						return FALSE;
					};
					if (dwSecurityBaseOffset >= lpHeaderSection[dwSecIndex].PointerToRawData + lpHeaderSection[dwSecIndex].SizeOfRawData)
					{
						GET_DIRECTORY_ENTRY(lpNtHeader, IMAGE_DIRECTORY_ENTRY_SECURITY) += dwRawSizeNeeded;
					};
				};

				lpHeaderSection[dwSecIndex].SizeOfRawData += dwRawSizeNeeded;
				lpNtHeader->OptionalHeader.SizeOfCode += dwRawSizeNeeded;
#if ! (defined(_M_X64) || defined(__amd64__))
				if (lpNtHeader->OptionalHeader.BaseOfData > lpNtHeader->OptionalHeader.BaseOfCode)
				{
					lpNtHeader->OptionalHeader.BaseOfData += dwRawSizeNeeded;
				};
#endif
			}
			else if (dwRawSizeNeeded && lpHeaderSection[dwSecIndex].PointerToRawData
				) lpHeaderSection[dwSecIndex].PointerToRawData += dwRawSizeNeeded;
		};

		if (!lpExecutableSection)
		{
			Utils::Printf::Fail("Unable to get any executable section IMAGE_SECTION_HEADER structure");
			return FALSE;
		};

		DWORD dwShellcodeRVA = 0;
		DWORD dwShellcodeSize = sizeof(bShellcode);
		if (!dwShellcodeOffset)
		{
			PIMAGE_SECTION_HEADER lpImageSectionHeader = IMAGE_FIRST_SECTION(lpNtHeader);
			PIMAGE_SECTION_HEADER lpNewSectionHeader = &lpImageSectionHeader[lpNtHeader->FileHeader.NumberOfSections];

			if ((DWORD_PTR)lpNewSectionHeader + sizeof(IMAGE_SECTION_HEADER) - (DWORD_PTR)lpPeFileInfo->lpDataBuffer
				> lpNtHeader->OptionalHeader.SizeOfHeaders)
			{
				Utils::Printf::Fail("No enough space at the headers for the new section");
				return FALSE;
			};

			ZeroMemory(
				lpNewSectionHeader,
				sizeof(IMAGE_SECTION_HEADER)
			);

			lpNewSectionHeader->VirtualAddress = lpNtHeader->OptionalHeader.SizeOfImage;
			lpNewSectionHeader->PointerToRawData = lpImageSectionHeader[lpNtHeader->FileHeader.NumberOfSections - 1].PointerToRawData
				+ lpImageSectionHeader[lpNtHeader->FileHeader.NumberOfSections - 1].SizeOfRawData;
			lpNewSectionHeader->SizeOfRawData = Utils::AlignUp(
				dwPaddedShellcodeSize,
				lpNtHeader->OptionalHeader.FileAlignment
			);
			lpNewSectionHeader->Misc.VirtualSize = dwPaddedShellcodeSize;
			lpNewSectionHeader->Characteristics =
				IMAGE_SCN_MEM_EXECUTE |
				IMAGE_SCN_MEM_READ;

			dwPaddedShellcodeSize = Utils::AlignUp(
				lpNewSectionHeader->Misc.VirtualSize,
				lpNtHeader->OptionalHeader.SectionAlignment
			);
			dwShellcodeOffset = lpNewSectionHeader->PointerToRawData;
			dwShellcodeRVA = lpNewSectionHeader->VirtualAddress;

			lpNtHeader->FileHeader.NumberOfSections++;
			lpNtHeader->OptionalHeader.SizeOfImage += dwPaddedShellcodeSize;

			BOOL IsSecurityExists = FALSE;
			if (!Load::PE::IsDirExists(lpNtHeader, IMAGE_DIRECTORY_ENTRY_SECURITY, &IsSecurityExists))
			{
				Utils::Printf::Fail("Cannot check if IMAGE_DIRECTORY_ENTRY_SECURITY table exists");
				return FALSE;
			};
			if (IsSecurityExists)
			{
				DWORD dwSecurityBaseOffset = 0;
				DWORD dwSecuritySize = 0;
				if (!Load::PE::GetDirectoryInfo(
					lpNtHeader,
					IMAGE_DIRECTORY_ENTRY_SECURITY,
					&dwSecurityBaseOffset,
					&dwSecuritySize,
					FALSE
				))
				{
					Utils::Printf::Fail("Unable to get the IMAGE_DIRECTORY_ENTRY_SECURITY table information");
					return FALSE;
				};
				if (dwSecurityBaseOffset >= dwShellcodeOffset)
				{
					GET_DIRECTORY_ENTRY(lpNtHeader, IMAGE_DIRECTORY_ENTRY_SECURITY) += dwPaddedShellcodeSize;
				};
			};
		}
		else dwShellcodeRVA = 
			dwShellcodeOffset - lpExecutableSection->PointerToRawData + lpExecutableSection->VirtualAddress;

		for (auto const& ObfuscateByNameElement : ObfuscateByNameArr)
		{
			CopyMemory(
				(LPVOID)((DWORD_PTR)lpShellCode + dwShellcodeSize),
				ObfuscateByNameElement.szOldApi,
				strlen(ObfuscateByNameElement.szOldApi) + 1
			);
			dwShellcodeSize += (DWORD)strlen(ObfuscateByNameElement.szOldApi) + 1;

			CopyMemory(
				(LPVOID)((uintptr_t)lpShellCode + dwShellcodeSize),
				(LPVOID)&ObfuscateByNameElement.dwFThunkRva,
				sizeof(ObfuscateByNameElement.dwFThunkRva)
			);
			dwShellcodeSize += (DWORD)sizeof(ObfuscateByNameElement.dwFThunkRva);

			DWORD lpApiImportOffset = (DWORD)ObfuscateByNameElement.lpOThunk->u1.AddressOfData;
			if (!Utils::RvaToOffset(lpNtHeader, lpApiImportOffset, &lpApiImportOffset))
			{
				Utils::Printf::Fail("Unable to get the IMAGE_IMPORT_BY_NAME structure pointer");
				return FALSE;
			};

			if (lpApiImportOffset >= 
				(lpExecutableSection->PointerToRawData + lpExecutableSection->SizeOfRawData - dwRawSizeNeeded))
			{
				lpApiImportOffset -= dwRawSizeNeeded;
			};

			PIMAGE_IMPORT_BY_NAME lpApiImport = (PIMAGE_IMPORT_BY_NAME)((DWORD_PTR)lpPeFileInfo->lpDataBuffer + lpApiImportOffset);
			LPVOID lpOldApi = (PCHAR)lpApiImport->Name;

			if (!Utils::IsValidReadPtr(
				lpApiImport,
				sizeof(IMAGE_IMPORT_BY_NAME)
			))
			{
				Utils::Printf::Fail("Invalid import directory");
				return FALSE;
			};

			ZeroMemory(
				lpApiImport->Name,
				(SIZE_T)strlen((PCHAR)lpApiImport->Name) + 1
			);

			if (strlen(ObfuscateByNameElement.szNewApi) > strlen(ObfuscateByNameElement.szOldApi))
			{
				CopyMemory(
					(LPVOID)((uintptr_t)lpShellCode + dwNewApisArrayOffset),
					&lpApiImport->Hint,
					sizeof(lpApiImport->Hint)
				);
				ObfuscateByNameElement.lpOThunk->u1.AddressOfData = (uintptr_t)dwShellcodeRVA + (uintptr_t)dwNewApisArrayOffset;
				dwNewApisArrayOffset += (DWORD)sizeof(lpApiImport->Hint);

				CopyMemory(
					(LPVOID)((uintptr_t)lpShellCode + dwNewApisArrayOffset),
					ObfuscateByNameElement.szNewApi,
					strlen(ObfuscateByNameElement.szNewApi) + 1
				);
				dwNewApisArrayOffset += (DWORD)strlen(ObfuscateByNameElement.szNewApi) + 1;
			}
			else
			{
				if (!Utils::SafeMemoryCopy(
					lpOldApi,
					(DWORD)strlen(ObfuscateByNameElement.szOldApi),
					(LPVOID)ObfuscateByNameElement.szNewApi,
					(DWORD)strlen(ObfuscateByNameElement.szNewApi)
				))
				{
					Utils::Printf::Fail("Unable to change the api name from %s to %s", ObfuscateByNameElement.szOldApi, ObfuscateByNameElement.szNewApi);
					return FALSE;
				};
			};
			Utils::Printf::Success("Patched %s to be %s", ObfuscateByNameElement.szOldApi, ObfuscateByNameElement.szNewApi);
		};
		dwShellcodeSize = dwNewApisArrayOffset;

		DWORD dwJmpDiff = dwShellcodeRVA - lpNtHeader->OptionalHeader.AddressOfEntryPoint - sizeof(bOriginalBytes);
		PBYTE bJmpDiffBytes = (PBYTE)& dwJmpDiff;

		BYTE bNewBytes[] =
		{
			0xE8,
			bJmpDiffBytes[0], bJmpDiffBytes[1],
			bJmpDiffBytes[2], bJmpDiffBytes[3]
		};
		if (!Utils::SafeMemoryCopy(
			lpEntry,
			sizeof(bOriginalBytes),
			(LPVOID)bNewBytes,
			sizeof(bOriginalBytes)
		))
		{
			Utils::Printf::Fail("Unable to write the new 5 bytes to the entry point");
			return FALSE;
		};

		LPVOID lpWholeShellCode = NULL;
		if (!(lpWholeShellCode = VirtualAlloc(
			NULL,
			dwPaddedShellcodeSize,
			MEM_COMMIT | MEM_RESERVE,
			PAGE_READWRITE
		)))
		{
			Utils::Reportf::ApiError("VirtualAlloc", "Cannot allocate the size of %d, for the shellcode", dwPaddedShellcodeSize);
			return FALSE;
		};

		if (!Utils::SafeMemoryCopy(
			lpWholeShellCode,
			dwPaddedShellcodeSize,
			lpShellCode,
			dwShellcodeSize
		))
		{
			Utils::Printf::Fail("Cannot write the shellcode");
			return FALSE;
		};

		if (!DeleteFileA(szOutFileName)) 
		{
			if (ERROR_FILE_NOT_FOUND != GetLastError()) {
				Utils::Reportf::ApiError("DeleteFileA", "Out file %s cannot be deleted", szOutFileName);
				return FALSE;
			}
		}

		HANDLE hFile = NULL;
		if (!(hFile = CreateFileA(
			szOutFileName,
			FILE_APPEND_DATA | GENERIC_READ,
			0,
			NULL,
			CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			NULL
		)) || INVALID_HANDLE_VALUE == hFile)
		{
			Utils::Reportf::ApiError("CreateFileA", "Cannot create the out file %s", szOutFileName);
			return FALSE;
		};

		DWORD dwWrittenBytes = 0;
		if (!WriteFile(
			hFile,
			lpPeFileInfo->lpDataBuffer,
			dwShellcodeOffset,
			&dwWrittenBytes,
			NULL
		) || dwWrittenBytes != dwShellcodeOffset)
		{
			Utils::Reportf::ApiError("WriteFile", "Cannot write to the out file %s", szOutFileName);
			return FALSE;
		};

		if (!WriteFile(
			hFile,
			lpWholeShellCode,
			dwPaddedShellcodeSize,
			&dwWrittenBytes,
			NULL
		) || dwWrittenBytes != dwPaddedShellcodeSize)
		{
			Utils::Reportf::ApiError("WriteFile", "Cannot write to the out file %s", szOutFileName);
			return FALSE;
		};

		dwShellcodeOffset = Utils::AlignUp(
			dwShellcodeOffset,
			lpNtHeader->OptionalHeader.FileAlignment
		);

		if (!WriteFile(
			hFile,
			(LPCVOID)((DWORD_PTR)lpPeFileInfo->lpDataBuffer + dwShellcodeOffset),
			lpPeFileInfo->dwSize - dwShellcodeOffset,
			&dwWrittenBytes,
			NULL
		) || dwWrittenBytes != lpPeFileInfo->dwSize - dwShellcodeOffset)
		{
			Utils::Reportf::ApiError("WriteFile", "Cannot write to the out file %s", szOutFileName);
			return FALSE;
		};

		LARGE_INTEGER ZeroPointer;
		ZeroMemory
		(
			&ZeroPointer,
			sizeof(LARGE_INTEGER)
		);

		if (!SetFilePointerEx
		(
			hFile,
			ZeroPointer,
			NULL,
			FILE_BEGIN
		))
		{
			Utils::Reportf::ApiError("SetFilePointerEx", "Error setting pointer at the begin of the saved file");
			return FALSE;
		};

		LARGE_INTEGER u32FileSize;
		if (!GetFileSizeEx(
			hFile,
			&u32FileSize
		))
		{
			Utils::Reportf::ApiError("GetFileSizeEx", "Error while getting the size of the saved file");
			return FALSE;
		};

		DWORD dwAlignedSize = Utils::AlignUp(
			(DWORD)u32FileSize.QuadPart,
			sizeof(DWORD)
		);

		LPVOID bFileContent = NULL;
		if (!(bFileContent = VirtualAlloc(
			NULL,
			dwAlignedSize,
			(MEM_COMMIT | MEM_RESERVE),
			PAGE_READWRITE
		)))
		{
			Utils::Reportf::ApiError("VirtualAlloc", "Error while allocating for the saved file %d", dwAlignedSize);
			return FALSE;
		};

		DWORD dwReadBytes = 0;
		if (!ReadFile(
			hFile,
			bFileContent,
			(DWORD)u32FileSize.QuadPart,
			&dwReadBytes,
			NULL
		) || dwReadBytes != u32FileSize.QuadPart)
		{
			Utils::Reportf::ApiError("ReadFile", "Error while reading the saved file %d, %d", dwReadBytes, u32FileSize.QuadPart);
			return FALSE;
		};

		DWORD dwCheckSumOffset = (DWORD)((uintptr_t)& lpNtHeader->OptionalHeader.CheckSum - (uintptr_t)lpPeFileInfo->lpDataBuffer);
		ULONGLONG CheckSum = 0;

		for (DWORD dwIndex = 0; dwIndex < u32FileSize.QuadPart / sizeof(DWORD); dwIndex++)
		{
			if (dwIndex == dwCheckSumOffset / sizeof(DWORD))
			{
				continue;
			};

			CheckSum = (CheckSum & 0xffffffff) + *(PDWORD)((uintptr_t)bFileContent + (ULONGLONG)dwIndex * sizeof(DWORD)) + (CheckSum >> 32);
			if (CheckSum > (2 ^ 32))
			{
				CheckSum = (CheckSum & 0xffffffff) + (CheckSum >> 32);
			};
		};

		CheckSum = (CheckSum & 0xffff) + (CheckSum >> 16);
		CheckSum = CheckSum + (CheckSum >> 16);
		CheckSum = CheckSum & 0xffff;
		CheckSum += u32FileSize.QuadPart;

		*(PDWORD)((uintptr_t)bFileContent + dwCheckSumOffset) = (DWORD)CheckSum;

		if (!Load::File::UnLoad(lpPeFileInfo, sizeof(RAW_FILE_INFO)))
		{
			Utils::Printf::Fail("Cannot unload the PE");
			return FALSE;
		};

		CloseHandle(hFile);
		hFile = NULL;

		if (!(hFile = CreateFileA(
			szOutFileName,
			GENERIC_WRITE | GENERIC_READ,
			0,
			NULL,
			TRUNCATE_EXISTING,
			FILE_ATTRIBUTE_NORMAL,
			NULL
		)) || INVALID_HANDLE_VALUE == hFile)
		{
			Utils::Reportf::ApiError("CreateFileA", "Cannot open the saved file");
			return FALSE;
		};

		if (!WriteFile(
			hFile,
			bFileContent,
			dwAlignedSize,
			&dwWrittenBytes,
			NULL
		) || dwWrittenBytes != dwAlignedSize)
		{
			Utils::Reportf::ApiError("WriteFile", "Cannot write to the saved file");
			return FALSE;
		};

		CloseHandle(hFile);
		return TRUE;
    }
    else 
    {
        Utils::Printf::Info("%s [in_file] [out_file] [target_api_0],[new_api_0] [target_api_1],[new_api_1] ...\n", argv[0]);
        return TRUE;

    };
};
