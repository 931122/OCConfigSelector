/********************************************************************
* file: x.c                             date: 二 2026-01-20 09:22:09*
*                                                                   *
* Description:                                                      *
*                                                                   *
*                                                                   *
* Maintainer: yxl  <yinxianglu1993@gmail.com>                       *
*                                                                   *
* This file is free software;                                       *
*   you are free to modify and/or redistribute it                   *
*   under the terms of the GNU General Public Licence (GPL).        *
*                                                                   *
* Last modified:                                                    *
*                                                                   *
* No warranty, no liability, use this at your own risk!             *
********************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>

#define MAX_FILES 32
#define MAX_NAME 128
#define TARGET_FILE L"config.plist"
#define COPY_BLOCK (64 * 1024) // 64KB 缓冲区提高效率

STATIC EFI_STATUS ReadKeyWait(EFI_INPUT_KEY *Key)
{
	UINTN Index;
	gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &Index);
	return gST->ConIn->ReadKeyStroke(gST->ConIn, Key);
}

STATIC EFI_STATUS CopyFile(EFI_FILE_PROTOCOL *Dir, CHAR16 *SrcName, CHAR16 *DstName)
{
	EFI_STATUS Status;
	EFI_FILE_PROTOCOL *SrcFile = NULL;
	EFI_FILE_PROTOCOL *DstFile = NULL;
	VOID *Buffer = NULL;
	UINTN BufferSize;
	UINT64 TotalWritten = 0;

	Status = Dir->Open(Dir, &SrcFile, SrcName, EFI_FILE_MODE_READ, 0);

	if (EFI_ERROR(Status))
		return Status;

	Status = Dir->Open(Dir, &DstFile, DstName, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);

	if (!EFI_ERROR(Status)) {
		Status = DstFile->Delete(DstFile); // 无论成功失败，句柄都会关闭
	}

	Status = Dir->Open(Dir, &DstFile, DstName,
	                   EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);

	if (EFI_ERROR(Status)) {
		SrcFile->Close(SrcFile);
		return Status;
	}

	Buffer = AllocatePool(COPY_BLOCK);

	if (Buffer == NULL) {
		SrcFile->Close(SrcFile);
		DstFile->Close(DstFile);
		return EFI_OUT_OF_RESOURCES;
	}

	while (TRUE) {
		BufferSize = COPY_BLOCK;
		Status = SrcFile->Read(SrcFile, &BufferSize, Buffer);

		if (EFI_ERROR(Status) || BufferSize == 0)
			break;

		UINTN WriteSize = BufferSize;
		Status = DstFile->Write(DstFile, &WriteSize, Buffer);

		if (EFI_ERROR(Status))
			break;

		TotalWritten += WriteSize; // 记录总写入长度
	}

	//【核心步骤】强制截断文件长度
	// 即使固件驱动有 Bug，显式设置 FileSize 也会强迫它丢弃末尾数据
	if (!EFI_ERROR(Status)) {
		UINTN InfoSize = 0;
		EFI_FILE_INFO *FileInfo = NULL;

		// 获取当前文件的 Info 结构体
		Status = DstFile->GetInfo(DstFile, &gEfiFileInfoGuid, &InfoSize, NULL);

		if (Status == EFI_BUFFER_TOO_SMALL) {
			FileInfo = AllocatePool(InfoSize);

			if (FileInfo != NULL) {
				Status = DstFile->GetInfo(DstFile, &gEfiFileInfoGuid, &InfoSize, FileInfo);

				if (!EFI_ERROR(Status)) {
					FileInfo->FileSize = TotalWritten; // 强制设置为实际写入的大小
					Status = DstFile->SetInfo(DstFile, &gEfiFileInfoGuid, InfoSize, FileInfo);
				}

				FreePool(FileInfo);
			}
		}
	}

	FreePool(Buffer);
	SrcFile->Close(SrcFile);
	DstFile->Flush(DstFile);
	DstFile->Close(DstFile);

	return Status;
}

// --- 菜单界面 ---
STATIC EFI_STATUS SelectMenu(CHAR16 Files[][MAX_NAME], UINTN Count, UINTN *Sel)
{
	EFI_INPUT_KEY Key;
	UINTN Pos = 0;

	while (TRUE) {
		gST->ConOut->ClearScreen(gST->ConOut);
		Print(L"OpenCore Config Switcher v1.0\n");
		Print(L"------------------------------\n");

		for (UINTN i = 0; i < Count; i++) {
			Print(i == Pos ? L" [%02d]   > %s\n" : L" [%02d]     %s\n", i + 1, Files[i]);
		}

		Print(L"\n[UP/DOWN] Select, [ENTER] Confirm, [ESC] Exit\n");

		ReadKeyWait(&Key);

		if (Key.ScanCode == SCAN_UP && Pos > 0)
			Pos--;

		else if (Key.ScanCode == SCAN_DOWN && Pos + 1 < Count)
			Pos++;

		else if (Key.ScanCode == SCAN_ESC)
			return EFI_ABORTED;

		else if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
			*Sel = Pos;
			return EFI_SUCCESS;
		}
	}
}

EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
	EFI_FILE_PROTOCOL *Root = NULL;
	EFI_FILE_PROTOCOL *EfiDir = NULL;
	EFI_FILE_PROTOCOL *OcDir = NULL;
	EFI_FILE_INFO *Info;
	UINTN InfoSize;
	CHAR16 Files[MAX_FILES][MAX_NAME];
	UINTN Count = 0;
	UINTN Sel;
	EFI_STATUS Status;

	gST->ConOut->ClearScreen(gST->ConOut);

	// 1. 获取文件系统
	Status = gBS->LocateProtocol(&gEfiSimpleFileSystemProtocolGuid, NULL, (VOID **)&Fs);

	if (EFI_ERROR(Status)) {
		Print(L"Error: FS protocol not found.\n");
		return Status;
	}

	Status = Fs->OpenVolume(Fs, &Root);

	if (EFI_ERROR(Status))
		return Status;

	// 2. 依次打开 EFI 和 OC 目录
	Status = Root->Open(Root, &EfiDir, L"EFI", EFI_FILE_MODE_READ, 0);

	if (EFI_ERROR(Status)) {
		Print(L"Error: /EFI not found.\n");
		return Status;
	}

	Status = EfiDir->Open(EfiDir, &OcDir, L"OC", EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);

	if (EFI_ERROR(Status)) {
		Print(L"Error: /EFI/OC not found.\n");
		return Status;
	}

	// 3. 扫描 config-*.plist 文件
	while (Count < MAX_FILES) {
		InfoSize = sizeof(EFI_FILE_INFO) + 256;
		Info = AllocateZeroPool(InfoSize);

		if (!Info)
			break;

		Status = OcDir->Read(OcDir, &InfoSize, Info);

		if (EFI_ERROR(Status) || InfoSize == 0) {
			FreePool(Info);
			break;
		}

		if (!(Info->Attribute & EFI_FILE_DIRECTORY)) {
			if (StrnCmp(Info->FileName, L"config-", 7) == 0
				// && StriEndsWith(Info->FileName, L".plist")
			    ) {
				StrCpyS(Files[Count], MAX_NAME, Info->FileName);
				Count++;
			}
		}

		FreePool(Info);
	}

	if (Count == 0) {
		Print(L"No 'config-*.plist' files found in /EFI/OC/.\n");
		goto Exit;
	}

	// 4. 显示菜单
	Status = SelectMenu(Files, Count, &Sel);

	if (EFI_ERROR(Status))
		goto Exit;

	// 5. 执行拷贝
	Print(L"Use %s as default config.plist...\n", Files[Sel]);
	Status = CopyFile(OcDir, Files[Sel], TARGET_FILE);

	if (!EFI_ERROR(Status)) {
		Print(L"Success!\nRebooting in 3 seconds...\n");
		gBS->Stall(3000000);
		gRT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);

	} else {
		Print(L"Error: set failed: %r\n", Status);
	}

Exit:

	if (OcDir)
		OcDir->Close(OcDir);

	if (EfiDir)
		EfiDir->Close(EfiDir);

	if (Root)
		Root->Close(Root);

	Print(L"Press any key to exit...\n");
	EFI_INPUT_KEY Key;
	ReadKeyWait(&Key);
	return Status;
}

#ifdef __cplusplus
};
#endif
/*********************** End Of File: x.c ***********************/
