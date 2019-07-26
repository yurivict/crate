#pragma once

void createJailsDirectoryIfNeeded(const char *subdir = ""); // subdir is assumed to include the leading slash when non-empty
void createCacheDirectoryIfNeeded();
