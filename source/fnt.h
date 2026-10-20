#pragma once
#include <stdio.h>
#include <vector>

struct FntName
{
	const char *name;
	unsigned int length;
	unsigned short directory;
};

bool WriteFntDirectory(FILE *output, unsigned int base, unsigned int id, unsigned int &entry_start,
	unsigned int first_file, unsigned int parent, const std::vector<FntName> &names);
