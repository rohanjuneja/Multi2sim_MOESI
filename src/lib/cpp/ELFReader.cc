/*
 *  Multi2Sim
 *  Copyright (C) 2013  Rafael Ubal (ubal@ece.neu.edu)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>

#include "ELFReader.h"
#include "Misc.h"


using namespace std;
using namespace Misc;


namespace ELFReader
{



/*
 * Class 'ELFSection'
 */


Section::Section(File *file, long pos)
{
	/* Initialize */
	this->file = file;
	buffer = 0;
	size = 0;

	/* Read section header */
	info = (Elf32_Shdr *) (file->buffer + pos);
	if (pos < 0 || pos + (long) sizeof(Elf32_Shdr) > file->size)
		fatal("%s: invalid position for section header",
				file->path.c_str());

	/* Get section contents, if section type is not SHT_NOBITS
	 * (sh_type = 8). */
	if (info->sh_type != 8)
	{
		/* Check valid range */
		if (info->sh_offset + info->sh_size > file->size)
			fatal("%s: section out of range",
					file->path.c_str());

		/* Internal buffer */
		buffer = file->buffer + info->sh_offset;
		size = info->sh_size;

		/* Set up string stream */
		stringbuf *buf = ss.rdbuf();
		buf->pubsetbuf(buffer, size);
	}
}



/*
 * Class 'ELFProgramHeader'
 */


ProgramHeader::ProgramHeader(File *file, long pos)
{
	/* Initialize */
	this->file = file;

	/* Read program header */
	info = (Elf32_Phdr *) (file->buffer + pos);
	if (pos < 0 || pos + (long) sizeof(Elf32_Phdr) > file->size)
		fatal("%s: invalid position for program header",
				file->path.c_str());
}



/*
 * Class 'ELFSymbol'
 */

Symbol::Symbol(File *file, Section *section, long pos)
{
	/* Initialize */
	this->file = file;
	this->section = section;

	/* Read symbol */
	info = (Elf32_Sym *) (section->buffer + pos);
	if (pos < 0 || pos + (long) sizeof(Elf32_Sym) > section->size)
		fatal("%s: invalid position for symbol",
				file->path.c_str());

	/* Get section with symbol name */
	unsigned name_section_index = section->info->sh_link;
	if (name_section_index >= file->section_list.size())
		fatal("%s: invalid index for symbol name section",
				file->path.c_str());
	Section *name_section = file->section_list[name_section_index];

	/* Get symbol name */
	if (info->st_name >= name_section->size)
		fatal("%s: invalid symbol name offset",
				file->path.c_str());
	name = name_section->buffer + info->st_name;
}


bool Symbol::Compare(Symbol *a, Symbol *b)
{
	int bind_a;
	int bind_b;

	/* Sort by value */
	if (a->info->st_value < b->info->st_value)
		return true;
	else if (a->info->st_value > b->info->st_value)
		return false;

	/* Sort symbol with the same address as per their
	 * ST_BIND field in st_info (bits 4 to 8) */
	bind_a = (a->info->st_info >> 4) & 0xf;
	bind_b = (a->info->st_info >> 4) & 0xf;
	if (bind_a < bind_b)
		return true;
	else if (bind_a > bind_b)
		return false;
	
	/* Sort alphabetically */
	return a->name < b->name;
}


#if 0
static void ELFReaderReadSymbolSection(ELFReader *self, ELFSection *section)
{
	Elf32_Sym *sym;
	ELFSection *symbol_names_section;
	ELFSymbol *symbol;
	
	int i;
	int count;

	/* Read symbol table section */
	symbol_names_section = asELFSection(ArrayGet(self->section_array,
			section->info->sh_link));
	assert(symbol_names_section);
	elf_reader_debug("  section '%s' is symbol table with names in section '%s'\n",
		section->name->text, symbol_names_section->name->text);

	/* Insert symbols */
	count = section->info->sh_size / sizeof(Elf32_Sym);
	for (i = 0; i < count; i++)
	{
		/* Read symbol. Don't create symbol if name is empty. */
		sym = (Elf32_Sym *) section->buffer->ptr + i;
		if (* (char *) (symbol_names_section->buffer->ptr
				+ sym->st_name) == '\0')
			continue;

		/* Create symbol */
		symbol = new(ELFSymbol);
		symbol->sym = sym;
		symbol->name = symbol_names_section->buffer->ptr + sym->st_name;
		assert(sym->st_name < symbol_names_section->buffer->size);

		/* Add symbol to list */
		ArrayAdd(self->symbol_array, asObject(symbol));
	}
}


ELFSymbol *ELFReaderGetSymbolByAddress(ELFReader *self, unsigned int address,
		unsigned int *offset_ptr)
{
	ELFSymbol *symbol;
	ELFSymbol *prev_symbol;
	
	int min;
	int max;
	int mid;

	/* Empty symbol table */
	if (!self->symbol_array->count)
		return NULL;

	/* All symbols in the table have a higher address */
	symbol = asELFSymbol(ArrayGet(self->symbol_array, 0));
	if (address < symbol->sym->st_value)
		return NULL;

	/* Binary search */
	min = 0;
	max = self->symbol_array->count;
	while (min + 1 < max)
	{
		mid = (max + min) / 2;
		symbol = asELFSymbol(ArrayGet(self->symbol_array, mid));
		if (symbol->sym->st_value > address)
		{
			max = mid;
		}
		else if (symbol->sym->st_value < address)
		{
			min = mid;
		}
		else
		{
			min = mid;
			break;
		}
	}

	/* Invalid symbol */
	symbol = asELFSymbol(ArrayGet(self->symbol_array, min));
	if (!symbol->sym->st_value)
		return NULL;

	/* Go backwards to find first symbol with that address */
	for (;;)
	{
		min--;
		prev_symbol = asELFSymbol(ArrayGet(self->symbol_array, min));
		if (!prev_symbol || prev_symbol->sym->st_value !=
				symbol->sym->st_value)
			break;
		symbol = prev_symbol;
	}

	/* Return the symbol and its address */
	if (offset_ptr)
		*offset_ptr = address - symbol->sym->st_value;
	return symbol;
}


ELFSymbol *ELFReaderGetSymbolByName(ELFReader *self, char *name)
{
	ELFSymbol *symbol;

	/* Search */
	ArrayForEach(self->symbol_array, symbol, ELFSymbol)
		if (!strcmp(symbol->name, name))
			return symbol;
	
	/* Not found */
	return NULL;
}


ELFBuffer *ELFReaderReadSymbolContent(ELFReader *self, ELFSymbol *symbol)
{
	ELFSection *section;
	ELFBuffer *buffer;

	/* Get section where the symbol is pointing */
	section = asELFSection(ArrayGet(self->section_array,
			symbol->sym->st_shndx));
	if (!section || symbol->sym->st_value + symbol->sym->st_size
			> section->info->sh_size)
		return NULL;

	/* Create buffer object */
	buffer = new(ELFBuffer, section->buffer->ptr + symbol->sym->st_value,
			symbol->sym->st_size);
	return buffer;
}


#endif


/*
 * Class 'File'
 */

static const char *err_64bit =
	"\tThe ELF file being loaded is a 64-bit file, currently not supported\n"
	"\tby Multi2Sim. If you are compiling your own source code on a 64-bit\n"
	"\tmachine, please use the '-m32' flag in the gcc command-line. If you\n"
	"\tget compilation errors related with missing '.h' files, check that\n"
	"\tthe 32-bit gcc package associated with your Linux distribution is\n"
	"\tinstalled.\n";

void File::ReadHeader()
{
	/* Read ELF header */
	info = (Elf32_Ehdr *) buffer;
	if (size < (long) sizeof(Elf32_Ehdr))
		fatal("%s: invalid ELF file", path.c_str());

	/* Check that file is a valid ELF file */
	if (strncmp((char *) info->e_ident, ELFMAG, 4))
		fatal("%s: invalid ELF file", path.c_str());

	/* Check that ELF file is a 32-bit object */
	if (info->e_ident[EI_CLASS] == ELFCLASS64)
		fatal("%s: 64-bit ELF not supported.\n%s",
			path.c_str(), err_64bit);
}


void File::ReadSections()
{
	/* Check section size and number */
	if (!info->e_shnum || info->e_shentsize != sizeof(Elf32_Shdr))
		fatal("%s: number of sections is 0 or section size is not %d",
			path.c_str(), (int) sizeof(Elf32_Shdr));

	/* Read section headers */
	for (int i = 0; i < info->e_shnum; i++)
	{
		Section *section = new Section(this, info->e_shoff +
				i * info->e_shentsize);
		section_list.push_back(section);
	}

	/* Read string table */
	if (info->e_shstrndx >= info->e_shnum)
		fatal("%s: invalid string table index", path.c_str());
	string_table = section_list[info->e_shstrndx];
	if (string_table->info->sh_type != 3)
		fatal("%s: invalid string table type", path.c_str());

	/* Read section names */
	for (auto it = section_list.begin(); it != section_list.end(); ++it)
		(*it)->name = string_table->buffer + (*it)->info->sh_name;
}


void File::ReadProgramHeaders()
{
	/* Check program header size */
	if (info->e_phentsize != sizeof(Elf32_Phdr))
		fatal("%s: program header size %d (should be %d)",
				path.c_str(), info->e_phentsize,
				(int) sizeof(Elf32_Phdr));
	
	/* Read program headers */
	for (int i = 0; i < info->e_phnum; i++)
	{
		ProgramHeader *ph = new ProgramHeader(this, info->e_phoff +
				i * info->e_phentsize);
		program_header_list.push_back(ph);
	}
}


void File::ReadSymbols()
{
	/* Load symbols from sections */
	for (auto it = section_list.begin(); it != section_list.end(); ++it)
	{
		Section *section = *it;
		if (section->info->sh_type != 2 &&
				section->info->sh_type != 11)
			continue;

		/* Read symbol table */
		int num_symbols = section->info->sh_size / sizeof(Elf32_Sym);
		for (int i = 0; i < num_symbols; i++)
		{
			/* Create symbol */
			Symbol *symbol = new Symbol(this, section,
					i * sizeof(Elf32_Sym));

			/* Discard empty symbol */
			if (symbol->name == "")
			{
				delete symbol;
				continue;
			}

			/* Add symbol */
			symbol_list.push_back(symbol);
		}
	}

	/* Sort */
	sort(symbol_list.begin(), symbol_list.end(), Symbol::Compare);
}


File::File(std::string path)
{
	/* Initialize */
	this->path = path;

	/* Open file */
	ifstream f(path);
	if (!f)
		fatal("%s: cannot open file", path.c_str());

	/* Get file size */
	f.seekg(0, ios_base::end);
	size = f.tellg();
	f.seekg(0, ios_base::beg);

	/* Load entire file into buffer and close */
	buffer = new char[size];
	f.read(buffer, size);
	f.close();

	/* Make string stream point to buffer */
	stringbuf *buf = ss.rdbuf();
	buf->pubsetbuf(buffer, size);

	/* Read content */
	ReadHeader();
	ReadSections();
	ReadProgramHeaders();
	ReadSymbols();
}


File::~File(void)
{
	/* Free sections */
	for (auto it = section_list.begin(); it != section_list.end(); ++it)
		delete *it;

	/* Free program headers */
	for (auto it = program_header_list.begin();
			it != program_header_list.end(); ++it)
		delete *it;

	/* Free symbols */
	for (auto it = symbol_list.begin(); it != symbol_list.end(); ++it)
		delete *it;

	/* Free content */
	delete buffer;
}


ostream &operator<<(ostream &os, const File &file)
{
	/* Header */
	os << "ELF header:\n";
	os << "  ehdr.e_ident: EI_CLASS=" << (int) file.info->e_ident[5] <<
			", EI_DATA=" << (int) file.info->e_ident[5] <<
			", EI_VERSION=" << (int) file.info->e_ident[6] << "\n";
	os << "  ehdr.e_type: " << file.info->e_type << "\n";
	os << "  ehdr.e_machine: " << file.info->e_machine << "\n";
	os << "  ehdr.e_entry: 0x" << hex << file.info->e_entry << "\n";
	os << "  ehdr.e_phoff: " << file.info->e_phoff << "\n";
	os << "  ehdr.e_shoff: " << file.info->e_shoff << "\n";
	os << "  ehdr.e_phentsize: " << file.info->e_phentsize << "\n";
	os << "  ehdr.e_phnum: " << file.info->e_phnum << "\n";
	os << "  ehdr.e_shentsize: " << file.info->e_shentsize << "\n";
	os << "  ehdr.e_shnum: " << file.info->e_shnum << "\n";
	os << "  ehdr.e_shstrndx: " << file.info->e_shstrndx << "\n";
	os << '\n';

	/* Dump section headers */
	os << "Section headers:\n";
	os << "  [Nr] type flags addr     offset        size     link name\n";
	os << string(80, '-') << '\n';
	for (unsigned i = 0; i < file.section_list.size(); i++)
	{
		Section *section = file.section_list[i];
		os << "  [" << setw(2) << i << "] ";
		os << setw(4) << section->info->sh_type << ' ';
		os << setw(5) << section->info->sh_flags << ' ';
		os << setfill('0') << setw(8) << section->info->sh_addr << ' ';
		os << setw(8) << section->info->sh_offset << ' ';
		os << setfill(' ') << setw(9) << section->info->sh_size << ' ';
		os << setw(8) << section->info->sh_link << ' ';
		os << section->name;
		os << '\n';
	}
	os << '\n';
	
	/* Dump program headers */
	os << "Program headers:\n";
	os << "idx type       offset   vaddr    paddr     "
			<< "filesz     memsz  flags align\n";
	os << string(80, '-') << '\n';
	int index = 0;
	for (auto it = file.program_header_list.begin();
			it != file.program_header_list.end(); ++it)
	{
		ProgramHeader *ph = *it;
		os << setw(3) << index << ' ';
		os << setw(8) << hex << ph->info->p_type << ' ';
		os << setw(8) << hex << ph->info->p_offset << ' ';
		os << setw(8) << hex << ph->info->p_vaddr << ' ';
		os << setw(8) << hex << ph->info->p_paddr << ' ';
		os << setw(9) << ph->info->p_filesz << ' ';
		os << setw(9) << ph->info->p_memsz << ' ';
		os << setw(6) << ph->info->p_flags << ' ';
		os << ph->info->p_align << ' ';
		os << '\n';
		++index;
	}
	os << '\n';

	/* Dump */
	os << "Symbol table:\n";
	os << setw(40) << "name" << " "
			<< setw(15) << "section" << " "
			<< setw(12) << "value" << " "
			<< setw(12) << "size" << "\n";
	os << string(80, '-') << '\n';
	for (auto it = file.symbol_list.begin();
			it != file.symbol_list.end(); ++it)
	{
		Symbol *symbol = *it;
		
		/* Symbol name */
		os << setiosflags(ios::left);
		os << setw(40) << setiosflags(ios::left) << symbol->name << " ";
		os << resetiosflags(ios::left);

		/* Print section */
		unsigned section_index = symbol->info->st_shndx;
		if (section_index < file.section_list.size())
			os << setw(15) <<
					file.section_list[section_index]->name
					<< " ";
		else
			os << setw(15) << section_index << " ";

		/* Rest */
		os << setw(10) << hex << symbol->info->st_value << ' ';
		os << setw(12) << symbol->info->st_size << '\n';
	}
	os << '\n';

	/* Done */
	return os;
}


} /* namespace ELFReader */
