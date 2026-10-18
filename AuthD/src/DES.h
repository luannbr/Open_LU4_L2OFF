#pragma once

void DesKeyInit(char* key);
void DesWriteBlock(void* buffer, int length);
void DesReadBlock(void* buffer, int length);
