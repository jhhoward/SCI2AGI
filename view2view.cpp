#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <vector>
#include "lodepng.cpp"

using namespace std;

void DumpCell(const char* name, vector<uint8_t>& data, int width, int height);

bool verbose = false;
uint8_t* viewData;
long viewDataLength;

struct ViewHeader
{
	uint16_t header;
	uint16_t numGroups;
	uint16_t mirrorMask;
	uint16_t unused1;
	uint16_t unused2;
};

struct CellList
{
	uint16_t numCells;
	uint16_t unused;
	uint16_t images[1];
};

struct ImageCell
{
	uint16_t width;
	uint16_t height;
	int8_t offsetX;
	int8_t offsetY;
	uint8_t transparency;
	uint8_t data[1];
};

struct OutputCell
{
	int width, height;
	vector<uint8_t> pixels;
	vector<uint8_t> compressed;
	uint8_t transparency;
	uint8_t mirrorMask;
	
	int CalculateSize()
	{
		return 3 + compressed.size();
	}
};

struct OutputGroup
{
	vector<OutputCell> cells;
	int mirrorIndex;
	int sciImportIndex;
	int writeIndex;
	
	int CalculateHeaderSize()
	{
		return 1 + 2 * cells.size();
	}
	
	int CalculateTotalSize()
	{
		int result = CalculateHeaderSize();
		for(OutputCell& cell : cells)
		{
			result += cell.CalculateSize();
		}
		return result;
	}
};	

struct OutputView
{
	vector<OutputGroup> groups;
};

uint8_t EGAPalette[] = 
{
	0x00, 0x00, 0x00,
	0x00, 0x00, 0xaa,
	0x00, 0xaa, 0x00,
	0x00, 0xaa, 0xaa,
	0xaa, 0x00, 0x00,
	0xaa, 0x00, 0xaa,
	0xaa, 0x55, 0x00,
	0xaa, 0xaa, 0xaa,
	0x55, 0x55, 0x55,
	0x55, 0x55, 0xff,
	0x55, 0xff, 0x55,
	0x55, 0xff, 0xff,
	0xff, 0x55, 0x55,
	0xff, 0x55, 0xff,
	0xff, 0xff, 0x55,
	0xff, 0xff, 0xff
};

void WriteByte(FILE* fs, uint8_t byte)
{
	fwrite(&byte, 1, 1, fs);
}

void WriteWord(FILE* fs, uint16_t word)
{
	fwrite(&word, 2, 1, fs);
}

int main(int argc, char* argv[])
{
	const char* inputPath = nullptr;
	const char* outputPath = nullptr;
	bool dumpToPng = false;
	
	for(int arg = 1; arg < argc; arg++)
	{
		if(!stricmp(argv[arg], "-o"))
		{
			if(arg + 1 < argc)
			{
				if(outputPath)
				{
					printf("Output file specified more than once\n");
					return 1;
				}
				
				if(argv[arg + 1][0] == '-')
				{
					printf("No output path specified after -o\n");
					return 1;
				}
				outputPath = argv[arg + 1];
				arg++;
			}
			else
			{
				printf("No output path specified after -o\n");
				return 1;
			}
		}
		else if(!stricmp(argv[arg], "-d"))
		{
			dumpToPng = true;
		}
		else if(!stricmp(argv[arg], "-v"))
		{
			verbose = true;
		}
		else
		{
			if(!inputPath)
			{
				inputPath = argv[arg];
			}
			else
			{
				printf("Unexpected argument %s\n", argv[arg]);
				return 1;
			}
		}
	}
	
	if(!inputPath)
	{
		printf("Usage: pic2pic [options] [input file]\n"
				"-o [path] To specify output path\n"
				"-d To dump files to PNG\n"
				"-v verbose mode\n"
				"-y [value] offset y output\n");
		return 1;
	}
	
	if(!outputPath)
	{
		outputPath = "output.view";
	}
	
	FILE* fileStream = fopen(inputPath, "rb");
	if(!fileStream)
	{
		printf("Could not open %s\n", inputPath);
		return 1;
	}
	
	fseek(fileStream, 0, SEEK_END);
	viewDataLength = ftell(fileStream);
	fseek(fileStream, 0, SEEK_SET);
	viewData = new uint8_t[viewDataLength];
	fread(viewData, viewDataLength, 1, fileStream);
	fclose(fileStream);
	
	OutputView outputView;
	uint8_t* viewDataPtr = viewData + 2;
	
	ViewHeader* header = (ViewHeader*)(viewData);
	if(verbose)
	{
		printf("Num groups: %d\n", header->numGroups);
	}
	
	uint16_t* cellLists = (uint16_t*)(viewData + sizeof(ViewHeader));
	
	for(int g = 0; g < header->numGroups; g++)
	{
		CellList* cellList = (CellList*)(viewDataPtr + cellLists[g]);
		bool isMirrored = (header->mirrorMask & (1 << g)) != 0;
		
		OutputGroup outputGroup;
		outputGroup.sciImportIndex = cellLists[g];
		outputGroup.mirrorIndex = -1;
		
		if(isMirrored)
		{
			for(int m = 0; m < outputView.groups.size(); m++)
			{
				if(outputView.groups[m].sciImportIndex == outputGroup.sciImportIndex)
				{
					outputGroup.mirrorIndex = m;
					
					OutputGroup& mirrorGroup = outputView.groups[m];
					for(OutputCell& cell : mirrorGroup.cells)
					{
						cell.mirrorMask |= (1 << g);
					}
					
					break;
				}
			}
			outputView.groups.push_back(outputGroup);
			continue;
		}
		
		if(verbose)
		{
			printf("Group %d has %d cells\n", g, cellList->numCells);
		}
		
		for(int c = 0; c < cellList->numCells; c++)
		{
			ImageCell* cell = (ImageCell*)(viewDataPtr + cellList->images[c]);
			int required = cell->width * cell->height;
			vector<uint8_t> uncompressedCell;
			uint8_t* ptr = cell->data;
			
			while(uncompressedCell.size() < required)
			{
				uint8_t pair = *ptr++;
				uint8_t colour = pair & 0xf;
				uint8_t count = pair >> 4;
				
				while(count)
				{
					uncompressedCell.push_back(colour);
					count--;
				}
			}
			
			OutputCell outputCell;
			outputCell.width = (cell->width + 1) / 2;
			outputCell.height = cell->height;
			outputCell.transparency = cell->transparency;
			
			for(int j = 0; j < outputCell.height; j++)
			{
				for(int i = 0; i < outputCell.width; i++)
				{
					if(i * 2 + 1 < cell->width && 0)
					{
						uint8_t left = uncompressedCell[j * cell->width + i * 2];
						uint8_t right = uncompressedCell[j * cell->width + i * 2 + 1];
						
						#if 0
						if(i > outputCell.width / 2)
						{
							outputCell.pixels.push_back(left);
						}
						else
						{
							outputCell.pixels.push_back(right);
						}
						
						#else 
						if(left == cell->transparency)
						{
							// Preserve transparency
							outputCell.pixels.push_back(right);
						}
						else
						{
							// Try to preserve details
							if(i * 2 + 2 < cell->width)
							{
								uint8_t rightRight = uncompressedCell[j * cell->width + i * 2 + 2];
								if(left == rightRight)
								{
									outputCell.pixels.push_back(right);
								}
								else
								{
									outputCell.pixels.push_back(left);
								}
							}
							else
							{
								outputCell.pixels.push_back(left);
							}
						}
						#endif
					}
					else
					{
						outputCell.pixels.push_back(uncompressedCell[j * cell->width + i * 2]);
					}
				}
			}	
			
			for(int j = 0; j < outputCell.height; j++)
			{
				uint8_t colour = 0;
				uint8_t count = 0;
				
				for(int i = 0; i < outputCell.width; i++)
				{
					uint8_t pixel = outputCell.pixels[j * outputCell.width + i];
					if(count == 0)
					{
						colour = pixel;
						count = 1;
					}
					else
					{
						if(pixel == colour && count < 15)
						{
							count++;
						}
						else
						{
							outputCell.compressed.push_back((colour << 4) | count);
							colour = pixel;
							count = 1;
						}
					}
				}
				
				if(count > 0)
				{
					outputCell.compressed.push_back((colour << 4) | count);
				}			
				
				outputCell.compressed.push_back(0);
			}

			outputGroup.cells.push_back(outputCell);
			
			if(dumpToPng)
			{
				char filename[128];
				sprintf(filename, "cell-%d-%d.png", g, c);
				DumpCell(filename, outputCell.pixels, outputCell.width, outputCell.height);
				//DumpCell(filename, uncompressedCell, cell->width, cell->height);
			}
		}
		
		outputView.groups.push_back(outputGroup);
	}
	
	// Write output
	FILE* outputFile = fopen(outputPath, "wb");
	if(!outputFile)
	{
		printf("Could not open %s\n", outputPath);
		return 1;
	}
	
	// Header
	WriteByte(outputFile, 1);		// Unknown - version?
	WriteByte(outputFile, 1);		// Unknown
	WriteByte(outputFile, (uint8_t) outputView.groups.size());		// Num groups
	WriteWord(outputFile, 0);		// Description
	
	int groupOffset = ftell(outputFile) + outputView.groups.size() * 2;
	
	// Write group pointers
	for(int g = 0; g < outputView.groups.size(); g++)
	{
		OutputGroup& group = outputView.groups[g];
		
		if(group.mirrorIndex != -1)
		{
			WriteWord(outputFile, (uint16_t) outputView.groups[group.mirrorIndex].writeIndex);
		}
		else
		{
			group.writeIndex = groupOffset;
			WriteWord(outputFile, (uint16_t) groupOffset);
			groupOffset += outputView.groups[g].CalculateTotalSize();
		}
	}
	
	// Write group data
	for(int g = 0; g < outputView.groups.size(); g++)
	{
		OutputGroup& group = outputView.groups[g];
		
		if(group.mirrorIndex != -1)
			continue;
		
		WriteByte(outputFile, (uint8_t) group.cells.size());

		int cellOffset = group.CalculateHeaderSize();
		
		// Write cell pointers
		for(int c = 0; c < group.cells.size(); c++)
		{
			WriteWord(outputFile, (uint16_t) cellOffset);
			cellOffset += group.cells[c].CalculateSize();
		}
		
		// Write cell data
		for(int c = 0; c < group.cells.size(); c++)
		{
			OutputCell& cell = group.cells[c];
			WriteByte(outputFile, (uint8_t) cell.width);
			WriteByte(outputFile, (uint8_t) cell.height);
			WriteByte(outputFile, (uint8_t) cell.transparency | (cell.mirrorMask << 4));
			
			for(uint8_t& data : cell.compressed)
			{
				WriteByte(outputFile, data);
			}
		}		
	}		
	
	delete[] viewData;
	fclose(outputFile);
	
	return 0;
}

void DumpCell(const char* name, vector<uint8_t>& data, int width, int height)
{
	vector<uint8_t> output;
	
	for(uint8_t& pixel : data)
	{
		output.push_back(EGAPalette[pixel * 3]);
		output.push_back(EGAPalette[pixel * 3 + 1]);
		output.push_back(EGAPalette[pixel * 3 + 2]);
		output.push_back(255);
	}
	
	lodepng::encode(name, output, width, height);
}
