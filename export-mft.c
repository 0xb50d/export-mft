#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define BUFFER_SIZE 512
#define FILE_RECORD_HEADER_SIZE 48

uint64_t Read(HANDLE hHandle, void *buffer, uint64_t from, uint64_t count, LPDWORD bytesAccessed) {
    LONG high = from >> 32;
    SetFilePointer(hHandle, from & 0xFFFFFFFF, &high, FILE_BEGIN);
    ReadFile(hHandle, buffer, count, bytesAccessed, NULL);
    return *bytesAccessed;
}

void dumpbyte(BYTE *buffer, uint64_t from, uint64_t to) {
    uint64_t i;
    for (i=from; i<to; i++) {
        printf("%02x ", (buffer[i]));
        if (0 == (i+1)%16) printf("\n");
    }
    printf("\n");
    return;
}

void ExportMFT(char* volume, char* outpath) {
    HANDLE hVolume;
    HANDLE hOutputFile;
    DWORD bytesRead;
    DWORD bytesWritten;
    DWORD attributeType;
    DWORD attributeSize;
    BYTE volumeBootRecord[BUFFER_SIZE];
    BYTE mftFileRecordHeader[BUFFER_SIZE];
    BYTE buffer[BUFFER_SIZE];
    BYTE* attributes;
    BYTE* dataAttribute;
    BYTE mftData[0x1000];
    ULONGLONG mftSize;
    BYTE* dataRuns;
    char* outputFilePath;
    int offsetToAttributes;
    int attributesRealSize;
    int currentOffset = 0;
    int totalBytesWritten = 0;
    int dataRunStringsOffset = 0;
    int startBytes;
    int lengthBytes;
    ULONGLONG dataRunBase = 0;
    ULONGLONG dataRunLength = 0;
    ULONGLONG clusterOffset = 0;

    // Open the volume
    char volumePath[8];
    sprintf(volumePath, "\\\\.\\%s:", volume);
    hVolume = CreateFile(volumePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hVolume == INVALID_HANDLE_VALUE) {
        printf("Unable to open the volume.\n");
        return;
    }

    Read(hVolume, &volumeBootRecord, 0, BUFFER_SIZE, &bytesRead);

    // Parse MFT offset from VBR
    int64_t mftOffset = *((int64_t*)(volumeBootRecord + 0x30));
    if (mftOffset < 0) {
        // Handle negative offset
        ULONGLONG volumeSectorSize = *((ULONGLONG*)(volumeBootRecord + 0xB));
        mftOffset += volumeSectorSize;
    }
    mftOffset *= 0x1000;

    Read(hVolume, &mftFileRecordHeader, mftOffset, sizeof(mftFileRecordHeader), &bytesRead);

    // Parse values from MFT file record header
    offsetToAttributes = *((short*)(mftFileRecordHeader + 0x14));
    attributesRealSize = *((int*)(mftFileRecordHeader + 0x18));
    
    // Allocate memory for the attributes
    attributes = (BYTE*)malloc(attributesRealSize - offsetToAttributes);
    if (attributes == NULL) {
        printf("Memory allocation failed.\n");
        CloseHandle(hVolume);
        return;
    } //*/

    memcpy(attributes, mftFileRecordHeader + offsetToAttributes, attributesRealSize - offsetToAttributes);

    // Find Data attribute
    do {
        attributeType = *((int*)(attributes + currentOffset));
        attributeSize = *((int*)(attributes + currentOffset + 4));
        currentOffset += attributeSize;
    } while (128 != attributeType);

    // Parse data attribute from all attributes
    dataAttribute = attributes + currentOffset - attributeSize;

    // Parse MFT size from data attribute
    mftSize = *((ULONGLONG*)(dataAttribute + 0x30));

    // Parse data runs from data attribute
    int offsetToDataRuns = *((short*)(dataAttribute + 0x20));
    dataRuns = dataAttribute + offsetToDataRuns;

    // Open the output file
    outputFilePath = (char*)malloc(MAX_PATH);
    if (outputFilePath == NULL) {
        printf("Memory allocation failed.\n");
        free(attributes);
        CloseHandle(hVolume);
        return;
    }

    strcpy(outputFilePath, outpath);
    strcat(outputFilePath, "\\");
    strcat(outputFilePath, "MFT-");
    strcat(outputFilePath, volume);
    strcat(outputFilePath, ".bin");

    hOutputFile = CreateFile(outputFilePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hOutputFile == INVALID_HANDLE_VALUE) {
        printf("Unable to create output file.\n");
        free(attributes);
        free(outputFilePath);
        CloseHandle(hVolume);
        return;
    }

    // Read and write the MFT data

    printf("Beginning export\n");
    printf("Exporting drive   : %s\n", volumePath);
    printf("Output path       : %s\n", outputFilePath);
    printf("Expected size     : %d\n\n", mftSize);

    do {
        startBytes = (int)dataRuns[dataRunStringsOffset] >> 4;
        lengthBytes = (int)dataRuns[dataRunStringsOffset] & 0xf;

        dataRunBase = 0;
        for (int i = startBytes; i > 0; i--) dataRunBase = dataRunBase << 8 | dataRuns[dataRunStringsOffset + lengthBytes + i];
        dataRunLength = 0;
        for (int i = lengthBytes; i > 0; i--) dataRunLength = dataRunLength << 8 | dataRuns[dataRunStringsOffset + i];
        
        clusterOffset += (dataRunBase * 0x1000);

        /*
        printf("dataRunStringsOffset : %d\n", dataRunStringsOffset);
        printf("startbytes           : %d\n", startBytes);
        printf("lengthbytes          : %d\n", lengthBytes);
        printf("datarunstart         : 0x%08x\n", dataRunBase);
        printf("datarunlength        : 0x%08x\n", dataRunLength);
        printf("clusterOffset        : %llu\n\n", clusterOffset);
        //*/ 

        int dr_count;
        for (dr_count = 0; dr_count < dataRunLength; dr_count++) {
            if (!Read(hVolume, &mftData, clusterOffset + (dr_count * sizeof(mftData)), sizeof(mftData), &bytesRead) || (bytesRead != sizeof(mftData))) {
                printf("Error reading MFT data.\n");
                free(attributes);
                free(outputFilePath);
                CloseHandle(hVolume);
                CloseHandle(hOutputFile);
                return;
            }

            // Write the MFT data to the output file
            if (!WriteFile(hOutputFile, mftData, sizeof(mftData), &bytesWritten, NULL) || bytesWritten != sizeof(mftData)) {
                printf("Error writing MFT data to output file.\n");
                free(attributes);
                free(outputFilePath);
                CloseHandle(hVolume);
                CloseHandle(hOutputFile);
                return;
            }

            totalBytesWritten += bytesWritten;
            
        }

        dataRunStringsOffset += startBytes + lengthBytes + 1;

    } while (totalBytesWritten < mftSize);

    printf("\n%d bytes written to : %s\n", totalBytesWritten, outputFilePath);

    // Close the handles
    free(attributes);
    free(outputFilePath);
    CloseHandle(hVolume);
    CloseHandle(hOutputFile);

    printf("MFT export completed successfully.\n\n");
}

void ExportMFT_AllDrive(char * outpath) {
    char volumeName[MAX_PATH];
    char volumePath[MAX_PATH];
    DWORD drives = GetLogicalDrives();
    DWORD mask = 1;
    DWORD driveType;
    char exportdrive[1];

    printf("Dumping all drive volumes.\n");

    for (char driveLetter = 'A'; driveLetter <= 'Z'; ++driveLetter) {
        if (drives & mask) {
            snprintf(volumePath, sizeof(volumePath), "%c:\\", driveLetter);
            driveType = GetDriveType(volumePath);
            if (driveType == DRIVE_FIXED) {
                if (GetVolumeInformation(volumePath, volumeName, MAX_PATH, NULL, NULL, NULL, NULL, 0)) {
                    sprintf(exportdrive, "%c", driveLetter);
                    ExportMFT(exportdrive, outpath);
                }
            }
        }
        mask <<= 1;
    }
}

int main(int argc, char* argv[]) {

    printf("\nExport-mft\n\n");

    if (argc < 3) {
        printf("Usage: %s <volume> <outputdir>\n", argv[0]);
        printf("Example : %s c C:\\\n", argv[0]);
        printf("Example : %s all C:\\\n", argv[0]);
        printf("\n");
        return 1;
    }

    if (strcmp(argv[1], "all") == 0) {
        ExportMFT_AllDrive(argv[2]);
    } else ExportMFT(argv[1], argv[2]);
    
    
    
    return 0;
}

