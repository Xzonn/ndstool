#include "ndstool.h"
#include "overlay.h"
#include <errno.h>
void ExtractDirectory(const char *prefix, unsigned int dir_id);
#ifdef NP3D_LIBRARY
#include <memory>
#include <string>
#include <vector>
#endif

#ifdef NP3D_LIBRARY
extern void (*ndsFileVisitor)(const char*, const char*, unsigned int, unsigned int, unsigned int);
extern void (*ndsDirectoryVisitor)(const char*, unsigned int, unsigned int, bool);
extern void ndsDirectoryCheck(const char*, unsigned int);
extern void ndsRequire(bool, const char*);

struct DirectoryRequest { std::string prefix; unsigned int id, parent; };
static std::vector<DirectoryRequest> *directoryRequests;
static unsigned int currentDirectory, currentParent, directoryCount;

static size_t ReadTable(void *output, size_t size, size_t count, FILE *input)
{
	unsigned int position = ftell(input);
	unsigned int length = size * count;
	bool in_fnt = position >= header.fnt_offset && position - header.fnt_offset <= header.fnt_size
		&& length <= header.fnt_size - (position - header.fnt_offset);
	bool in_fat = position >= header.fat_offset && position - header.fat_offset <= header.fat_size
		&& length <= header.fat_size - (position - header.fat_offset);
	ndsRequire(in_fnt || in_fat, "NDS table read outside FNT or FAT");
	size_t result = fread(output, size, count, input);
	ndsRequire(result == count, "NDS table is truncated");
	return result;
}
#define fread ReadTable
#endif

/*
 * MkDir
 */
void MkDir(char *name)
{
#ifdef __MINGW32__
	if (mkdir(name))
#else
	if (mkdir(name, S_IRWXU))
#endif
	{
		if (errno != EEXIST)
		{
			fprintf(stderr, "Cannot create directory '%s'.\n", name);
			exit(1);
		}
	}
}

/*
 * ExtractFile
 * if rootdir==0 nothing will be written
 */
void ExtractFile(const char *rootdir, const char *prefix, const char *entry_name, unsigned int file_id)
{
	unsigned int save_filepos = ftell(fNDS);
	
	// read FAT data
	fseek(fNDS, header.fat_offset + 8*file_id, SEEK_SET);
	unsigned_int top;
	fread(&top, 1, sizeof(top), fNDS);
	unsigned_int bottom;
	fread(&bottom, 1, sizeof(bottom), fNDS);
	unsigned int size = bottom - top;
	if (size > (1U << (17 + header.devicecap))) { fprintf(stderr, "File %u: Size is too big. FAT offset 0x%X contains invalid data.\n", file_id, header.fat_offset + 8*file_id); exit(1); }

#ifdef NP3D_LIBRARY
	if (ndsFileVisitor) {
		ndsFileVisitor(prefix, entry_name, file_id, top, size);
		fseek(fNDS, save_filepos, SEEK_SET);
		return;
	}
#endif

	// print file info
	if (!rootdir || verbose)
	{
		printf("%5u 0x%08X 0x%08X %9u %s%s\n", file_id, (int)top, (int)bottom, size, prefix, entry_name);
	}

	// extract file
	if (rootdir)
	{
		// make filename
		char filename[MAXPATHLEN];
		strcpy(filename, rootdir);
		strcat(filename, prefix);
		strcat(filename, entry_name);

		fseek(fNDS, top, SEEK_SET);
		FILE *fo = fopen(filename, "wb");
		if (!fo) { fprintf(stderr, "Cannot create file '%s'.\n", filename); exit(1); }
		while (size > 0)
		{
			unsigned char copybuf[1024];
			unsigned int size2 = (size >= sizeof(copybuf)) ? sizeof(copybuf) : size;
			fread(copybuf, 1, size2, fNDS);
			fwrite(copybuf, 1, size2, fo);
			size -= size2;
		}
		fclose(fo);
	}
	
	fseek(fNDS, save_filepos, SEEK_SET);	
}

/*
 * MatchName
 */
bool MatchName(char *name, char *mask, int level=0)
{
	char *a = name;
	char *b = mask;
	//for (int i=0; i<level; i++) printf("  "); printf("matching a='%s' b='%s'\n", a, b);
	while (1)
	{
		//for (int i=0; i<level; i++) printf("a=%s b=%s\n", a, b);
		if (*b == '*')
		{
			while (*b == '*') b++;
	//for (int i=0; i<level; i++) printf("  "); printf("* '%s' '%s'\n", a, b);
			bool match = false;
			char *a2 = a;
			do
			{
				if (MatchName(a2, b, level+1)) { a = a2; match = true; break; }
			} while (*a2++);
			if (!match) return false;
			//for (int i=0; i<level; i++) printf("  "); printf("matched a='%s' b='%s'\n", a, b);
		}
		//for (int i=0; i<level; i++) printf("  "); printf("a=%s b=%s\n", a, b);
		if (!*a && !*b) return true;
		if (*a && !*b) return false;
		if (!*a && *b) return false;
		if (*b != '?' && *a != *b) return false;
		a++; b++;
	}
}

/*
 * ExtractDirectory
 * filerootdir can be 0 for just listing files
 */
static void ExtractDirectoryContents(const char *prefix, unsigned int dir_id)
{
#ifdef NP3D_LIBRARY
	ndsDirectoryCheck(prefix, dir_id);
#endif
#ifdef NP3D_LIBRARY
	auto directoryBuffer = std::make_unique<char[]>(ndsDirectoryVisitor
		? strlen(prefix) + 130 + (filerootdir ? strlen(filerootdir) : 0) : MAXPATHLEN);
	char *strbuf = directoryBuffer.get();
#else
	char strbuf[MAXPATHLEN];
#endif
	unsigned int save_filepos = ftell(fNDS);

	fseek(fNDS, header.fnt_offset + 8*(dir_id & 0xFFF), SEEK_SET);
	unsigned_int entry_start;	// reference location of entry name
	fread(&entry_start, 1, sizeof(entry_start), fNDS);
	unsigned_short top_file_id;	// file ID of top entry 
	fread(&top_file_id, 1, sizeof(top_file_id), fNDS);
	unsigned_short parent_id;	// ID of parent directory or directory count (root)
	fread(&parent_id, 1, sizeof(parent_id), fNDS);
#ifdef NP3D_LIBRARY
	if (ndsDirectoryVisitor)
	{
		if (dir_id == 0xf000) directoryCount = parent_id;
		ndsRequire(directoryCount && directoryCount <= 4096 && directoryCount * 8 <= header.fnt_size,
			"Invalid FNT directory count");
		ndsRequire((dir_id & 0xfff) < directoryCount, "FNT directory ID outside table");
		ndsRequire(entry_start >= directoryCount * 8 && entry_start < header.fnt_size,
			"FNT directory names overlap or exceed table");
		ndsDirectoryVisitor(prefix, dir_id, dir_id == 0xf000 ? directoryCount : currentParent, true);
	}
#endif

	fseek(fNDS, header.fnt_offset + entry_start, SEEK_SET);

	// print directory name
	//printf("%04X ", dir_id);
	if ((!filerootdir || verbose)
#ifdef NP3D_LIBRARY
		&& !ndsFileVisitor
#endif
	)
	{
		printf("%s\n", prefix);
	}

	for (unsigned int file_id=top_file_id; ; file_id++)
	{
		unsigned char entry_type_name_length;
		fread(&entry_type_name_length, 1, sizeof(entry_type_name_length), fNDS);
		unsigned int name_length = entry_type_name_length & 127;
		bool entry_type_directory = (entry_type_name_length & 128) ? true : false;
		if (name_length == 0) {
#ifdef NP3D_LIBRARY
			if (ndsDirectoryVisitor) ndsRequire(!entry_type_directory, "Empty FNT directory name");
#endif
			break;
		}
	
		char entry_name[128];
		memset(entry_name, 0, 128);
		fread(entry_name, 1, entry_type_name_length & 127, fNDS);
#ifdef NP3D_LIBRARY
		if (!ndsDirectoryVisitor) ndsRequire(strlen(prefix) + name_length + 2 <= MAXPATHLEN, "NDS directory path exceeds parser limit");
		if (ndsDirectoryVisitor) ndsRequire(strlen(entry_name) == name_length, "NDS name contains NUL");
#endif
		if (entry_type_directory)
		{
			unsigned_short dir_id;
			fread(&dir_id, 1, sizeof(dir_id), fNDS);

			if (filerootdir)
			{
				strcpy(strbuf, filerootdir);
				strcat(strbuf, prefix);
				strcat(strbuf, entry_name);
				MkDir(strbuf);
			}

			strcpy(strbuf, prefix);
			strcat(strbuf, entry_name);
			strcat(strbuf, "/");
			ExtractDirectory(strbuf, dir_id);
#ifdef NP3D_LIBRARY
			if (ndsFileVisitor) file_id--;
#endif
		}
		else
		{
			if (1)
			{
				bool match = (filemask_num == 0);
				for (int i=0; i<filemask_num; i++)
				{
					//printf("%s %s\n", entry_name, filemasks[i]);
					if (MatchName(entry_name, filemasks[i])) { match = true; break; }
				}

				//if (!directorycreated)
				//{
				//}
				
				//printf("%d\n", match);

				if (match) ExtractFile(filerootdir, prefix, entry_name, file_id);
			}
		}
	}

#ifdef NP3D_LIBRARY
	if (ndsDirectoryVisitor) ndsDirectoryVisitor(prefix, dir_id, parent_id, false);
#endif
	fseek(fNDS, save_filepos, SEEK_SET);
}

void ExtractDirectory(const char *prefix, unsigned int dir_id)
{
#ifdef NP3D_LIBRARY
	if (ndsDirectoryVisitor)
	{
		if (directoryRequests) {
			directoryRequests->push_back({prefix, dir_id, currentDirectory});
			return;
		}
		std::vector<DirectoryRequest> requests{{prefix, dir_id, 0}};
		directoryRequests = &requests;
		while (!requests.empty()) {
			auto request = std::move(requests.back());
			requests.pop_back();
			currentDirectory = request.id;
			currentParent = request.parent;
			ExtractDirectoryContents(request.prefix.c_str(), request.id);
		}
		directoryRequests = nullptr;
		return;
	}
#endif
	ExtractDirectoryContents(prefix, dir_id);
}

/*
 * ExtractFiles
 */
#ifdef NP3D_LIBRARY
#undef fread
#endif
void ExtractFiles(char *ndsfilename)
{
//printf("%d\n", MatchName("hello", "h*el*lo")); exit(0);		// TEST
	fNDS = fopen(ndsfilename, "rb");
	if (!fNDS) { fprintf(stderr, "Cannot open file '%s'.\n", ndsfilename); exit(1); }
	fread(&header, 512, 1, fNDS);

	if (filerootdir)
	{
		MkDir(filerootdir);
	}

	ExtractDirectory("/", 0xF000);		// list or extract

	fclose(fNDS);
}

/*
 * ExtractOverlayFiles2
 */
 void ExtractOverlayFiles2(unsigned int overlay_offset, unsigned int overlay_size)
 {
 	OverlayEntry overlayEntry;

	if (overlay_size)
	{
		fseek(fNDS, overlay_offset, SEEK_SET);
		for (unsigned int i=0; i<overlay_size; i+=sizeof(OverlayEntry))
		{
			fread(&overlayEntry, 1, sizeof(overlayEntry), fNDS);
			int file_id = overlayEntry.id;
			char s[32]; sprintf(s, OVERLAY_FMT, file_id);
			ExtractFile(overlaydir, "/", s, file_id);
		}
	}
}


/*
 * ExtractOverlayFiles
 */
void ExtractOverlayFiles()
{
	fNDS = fopen(ndsfilename, "rb");
	if (!fNDS) { fprintf(stderr, "Cannot open file '%s'.\n", ndsfilename); exit(1); }
	fread(&header, 512, 1, fNDS);

	if (overlaydir)
	{
		MkDir(overlaydir);
	}

	ExtractOverlayFiles2(header.arm9_overlay_offset, header.arm9_overlay_size);
	ExtractOverlayFiles2(header.arm7_overlay_offset, header.arm7_overlay_size);

	fclose(fNDS);
}

/*
 * Extract
 */
void Extract(char *outfilename, bool indirect_offset, unsigned int offset, bool indirect_size, unsigned size, bool with_footer)
{
	fNDS = fopen(ndsfilename, "rb");
	if (!fNDS) { fprintf(stderr, "Cannot open file '%s'.\n", ndsfilename); exit(1); }
	fread(&header, 512, 1, fNDS);

	if (indirect_offset) offset = *((unsigned_int *)&header + offset/4);
	if (indirect_size) size = *((unsigned_int *)&header + size/4);
	
	fseek(fNDS, offset, SEEK_SET);

	FILE *fo = fopen(outfilename, "wb");
	if (!fo) { fprintf(stderr, "Cannot create file '%s'.\n", outfilename); exit(1); }
	
	unsigned char copybuf[1024];
	while (size > 0)
	{
		unsigned int size2 = (size >= sizeof(copybuf)) ? sizeof(copybuf) : size;
		fread(copybuf, 1, size2, fNDS);
		fwrite(copybuf, 1, size2, fo);
		size -= size2;
	}

	if (with_footer)
	{
		unsigned_int nitrocode;
		fread(&nitrocode, sizeof(nitrocode), 1, fNDS);
		if (nitrocode == 0xDEC00621)
		{
			// 0x2106C0DE, version info, reserved?
			for (int i=0; i<3; i++)		// write additional 3 words
			{
				fwrite(&nitrocode, sizeof(nitrocode), 1, fo);
				fread(&nitrocode, sizeof(nitrocode), 1, fNDS);	// next field
			}
		}
	}

	fclose(fo);
	fclose(fNDS);
}
