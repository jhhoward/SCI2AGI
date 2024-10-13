#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <vector>
#include "lodepng.cpp"

#define PIC_EGAPALETTE_SIZE  40
#define SCI_PATTERN_CODE_RECTANGLE 0x10
#define SCI_PATTERN_CODE_USE_TEXTURE 0x20
#define SCI_PATTERN_CODE_PENSIZE 0x07

#define AGI_PICTURE_WIDTH 160
#define AGI_PICTURE_HEIGHT 168
#define SCI_PICTURE_WIDTH 320
#define SCI_PICTURE_HEIGHT 190

#define AGI_LINE_INSTRUCTION 0xf6
#define AGI_SET_VISUAL 0xf0
#define AGI_DISABLE_VISUAL 0xf1
#define AGI_SET_PRIORITY 0xf2
#define AGI_DISABLE_PRIORITY 0xf3
#define AGI_FILL_INSTRUCTION 0xf8

#define AGI_OFFSET_Y -6
#define SCI_TO_AGI_X(x) ((x) / 2)
#define SCI_TO_AGI_Y(y) ((y) + AGI_OFFSET_Y)
#define AGI_TO_SCI_X(x) ((x) * 2)
#define AGI_TO_SCI_Y(y) ((y) - AGI_OFFSET_Y)

#define COLOUR_DISABLED 0xff

using namespace std;

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

struct Coord
{
	Coord() : x(0), y(0) {}
	Coord(int inX, int inY) : x(inX), y(inY) {}
	int x, y;
};

struct Canvas
{
	Canvas(int inWidth, int inHeight, uint8_t inDefaultColour);
	~Canvas();
	
	void SetPixel(int x, int y, uint8_t value);
	uint8_t GetPixel(int x, int y);
	
	void DrawLine(int x1, int y1, int x2, int y2, uint8_t colour);
	void Fill(int x, int y, uint8_t colour, vector<Coord>* filled = nullptr);
	void UnFill(vector<Coord>& filled);
	bool OkToFill(int x, int y);
	
	void DumpToPNG(const char* filename);
	
	int width, height;
	uint8_t* data;
	uint8_t defaultColour;
};

uint8_t* pictureData;
uint8_t* pictureDataPtr;
long pictureDataLength;
bool verbose = true;

bool mirroredFlag = false;
uint16_t patternCode;

uint8_t penColour = 0xff;
uint8_t priorityColour = 0xff;

uint8_t lastAgiInstruction = 0;

FILE* outputFile;

Canvas agiVisualCanvas(AGI_PICTURE_WIDTH, AGI_PICTURE_HEIGHT, 0xf);
Canvas sciVisualCanvas(SCI_PICTURE_WIDTH, SCI_PICTURE_HEIGHT, 0xf);
Canvas agiPriorityCanvas(AGI_PICTURE_WIDTH, AGI_PICTURE_HEIGHT, 4);
Canvas sciPriorityCanvas(SCI_PICTURE_WIDTH, SCI_PICTURE_HEIGHT, 4);

void WriteByte(uint8_t value)
{
	fwrite(&value, 1, 1, outputFile);
}

void EmitAgiInstruction(uint8_t instruction)
{
	lastAgiInstruction = instruction;
	WriteByte(instruction);
}

void EmitAgiFill(int16_t x, int16_t y)
{
	if(x >= 0 && y >= 0 && x < AGI_PICTURE_WIDTH && y < AGI_PICTURE_HEIGHT)
	{
		EmitAgiInstruction(AGI_FILL_INSTRUCTION);
		WriteByte((uint8_t)(x));
		WriteByte((uint8_t)(y));
	}
}

void EmitAgiLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
	static int16_t lastX = 0xff, lastY = 0xff;

	if(lastAgiInstruction != AGI_LINE_INSTRUCTION || lastX != x1 || lastY != y1)
	{
		EmitAgiInstruction(AGI_LINE_INSTRUCTION);	
		WriteByte((uint8_t)(x1));
		WriteByte((uint8_t)(y1));
	}
		
	WriteByte((uint8_t)(x2));
	WriteByte((uint8_t)(y2));
	
	lastX = x2;
	lastY = y2;
}

void EmitAgiSetVisual(uint8_t colour)
{
	if(colour == penColour)
	{
		return;
	}
	
	if(colour == COLOUR_DISABLED)
	{
		EmitAgiInstruction(AGI_DISABLE_VISUAL);
	}
	else
	{
		EmitAgiInstruction(AGI_SET_VISUAL);
		WriteByte(colour);
	}
	penColour = colour;
}

void EmitAgiSetPriority(uint8_t colour)
{
	if(colour == priorityColour)
	{
		return;
	}
	
	if(colour == COLOUR_DISABLED)
	{
		EmitAgiInstruction(AGI_DISABLE_PRIORITY);
	}
	else
	{
		EmitAgiInstruction(AGI_SET_PRIORITY);
		WriteByte(colour);
	}
	priorityColour = colour;
}

void EmitAgiPixel(int16_t x, int16_t y, uint8_t visualColour, uint8_t priorityColour)
{
	EmitAgiSetVisual(visualColour);
	EmitAgiSetPriority(priorityColour);
	EmitAgiInstruction(AGI_LINE_INSTRUCTION);
	WriteByte((uint8_t) x);
	WriteByte((uint8_t) y);
	WriteByte((uint8_t) x);
	WriteByte((uint8_t) y);
}

/*
void WriteAgiCoords(int16_t x, int16_t y)
{
	if(x < 0 || y < 0 || x >= 320 || y >= 200)
	{
		printf("Invalid coordinate: %d, %d\n", x, y);
	}
	
	uint8_t outX = (uint8_t) (x / 2);
	uint8_t outY = (uint8_t) y;
	
	if(outY >= 168)
		outY = 167;
	WriteByte(outX);
	WriteByte(outY);
}
*/

void DrawLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2)
{
	if(penColour != 0xff)
	{
		sciVisualCanvas.DrawLine(x1, y1, x2, y2, penColour);
	}
	if(priorityColour != 0xff)
	{
		sciPriorityCanvas.DrawLine(x1, y1, x2, y2, priorityColour);
	}
	
	x1 = SCI_TO_AGI_X(x1);
	y1 = SCI_TO_AGI_Y(y1);
	x2 = SCI_TO_AGI_X(x2);
	y2 = SCI_TO_AGI_Y(y2);
	
	// Clip
	if(y1 < 0 && y2 < 0)
		return;
	
	if(y1 >= 168 && y2 >= 168)
		return;
	
	if(y2 >= 168)
	{
		x2 = x1 + ((x2 - x1) * (167 - y1)) / (y2 - y1);
//		x2 = x1 + ((x2 - x1) * 167) / y2;
		y2 = 167;
	}
	if(y1 >= 168)
	{
		x1 = x2 + ((x1 - x2) * (167 - y2)) / (y1 - y2);
		//x1 = x2 + ((x1 - x2) * 167) / y1;
		y1 = 167;
	}
	
	if(y1 < 0)
	{
		x1 = x1 + ((x2 - x1) * (0 - y1)) / (y2 - y1);
		y1 = 0;
	}
	if(y2 < 0)
	{
		x2 = x2 + ((x1 - x2) * (0 - y2)) / (y1 - y2);
		y2 = 0;
	}
	
	EmitAgiLine(x1, y1, x2, y2);
	
	if(penColour != 0xff)
	{
		agiVisualCanvas.DrawLine(x1, y1, x2, y2, penColour);
	}
	if(priorityColour != 0xff)
	{
		agiPriorityCanvas.DrawLine(x1, y1, x2, y2, priorityColour);
	}
}

uint8_t NextByte()
{
	if(pictureDataPtr < pictureData + pictureDataLength)
	{
		return *pictureDataPtr++;
	}
	
	printf("Error: read past end of picture file\n");
	exit(1);
	return 0;
}

uint8_t PeekByte()
{
	return *pictureDataPtr;
}

uint8_t NextWord()
{
	return NextByte() | (NextByte() << 8);
}

bool IsInstruction(uint8_t code)
{
	return code >= 0xf0;
}

void GetAbsCoords(int16_t& x, int16_t& y)
{
	uint8_t pixel = NextByte();
	x = NextByte() + ((pixel & 0xF0) << 4);
	y = NextByte() + ((pixel & 0x00) << 8);
	if(mirroredFlag)
		x = 319 - x;
}

void GetRelCoordsMed(int16_t& x, int16_t& y) 
{
	uint8_t pixel = NextByte();
	if (pixel & 0x80) 
	{
		y -= (pixel & 0x7F);
	} 
	else 
	{
		y += pixel;
	}
	pixel = NextByte();
	if (pixel & 0x80) 
	{
		x -= (128 - (pixel & 0x7F)) * (mirroredFlag ? -1 : 1);
	} 
	else 
	{
		x += pixel * (mirroredFlag ? -1 : 1);
	}
}

void GetRelCoords(int16_t& x, int16_t& y) 
{
	uint8_t pixel = NextByte();
	if (pixel & 0x80) 
	{
		x -= ((pixel >> 4) & 7) * (mirroredFlag ? -1 : 1);
	} 
	else 
	{
		x += (pixel >> 4) * (mirroredFlag ? -1 : 1);
	}
	if (pixel & 0x08) 
	{
		y -= (pixel & 7);
	} 
	else 
	{
		y += (pixel & 7);
	}
}

void SetVisualColour()
{
	uint8_t colour = NextByte();
	
	if(verbose)
		printf("Set visual colour: %d\n", colour);
	
	colour &= 0xf;
	
	EmitAgiSetVisual(colour);
}

void DisableVisual()
{
	if(verbose)
		printf("Disable visual\n");

	EmitAgiSetVisual(COLOUR_DISABLED);
}

void SetPriorityColour()
{
	uint8_t colour = NextByte();
	
	if(verbose)
		printf("Set priority colour: %d\n", colour);
	
	/*int agiBand = 0;
	if(colour == 0)
		agiBand = 4;
	else if(colour == 15)
		agiBand = 15;
	else
	{
		int y = 42 + 11 * colour;
		if(y >= 167)
		{
			agiBand = 15;
		}
		else
		{
			agiBand = 4 + (y - 47) / 11;
		}
	}*/
	
	uint8_t agiBand = colour + 3;
	if(agiBand < 4)
		agiBand = 4;
	if(agiBand > 15)
		agiBand = 15;
	
	EmitAgiSetPriority(agiBand);
}

void DisablePriority()
{
	if(verbose)
		printf("Disable priority\n");
	
	EmitAgiSetPriority(COLOUR_DISABLED);
}

void SetControlColour()
{
	uint8_t colour = NextByte();
	
	if(verbose)
		printf("Set control colour: %d\n", colour);
	
	//WriteByte(0xf2);
	//WriteByte(colour ? 1 : 0);	
}

void DisableControl()
{
	if(verbose)
		printf("Disable control\n");

	//WriteByte(0xf3);
}

void GetPatternTexture(int16_t& patternTexture) 
{
	if (patternCode & SCI_PATTERN_CODE_USE_TEXTURE) 
	{
		patternTexture = (NextByte() >> 1) & 0x7f;
	}
}


void ShortPatterns()
{
	int16_t patternTexture;
	int16_t x, y;
	
	GetPatternTexture(patternTexture);
	GetAbsCoords(x, y);

	// Draw pattern here
	//vectorPattern(x, y, pic_color, pic_priority, pic_control, pattern_Code, pattern_Texture);
	
	while(!IsInstruction(PeekByte()))
	{
		GetPatternTexture(patternTexture);
		GetRelCoords(x, y);
		//vectorPattern(x, y, pic_color, pic_priority, pic_control, pattern_Code, pattern_Texture);
	}
}

void MediumRelativePatterns()
{
	int16_t patternTexture;
	int16_t x, y;
	
	GetPatternTexture(patternTexture);
	GetAbsCoords(x, y);

	// Draw pattern here
	//vectorPattern(x, y, pic_color, pic_priority, pic_control, pattern_Code, pattern_Texture);
	
	while(!IsInstruction(PeekByte()))
	{
		GetPatternTexture(patternTexture);
		GetRelCoordsMed(x, y);
		//vectorPattern(x, y, pic_color, pic_priority, pic_control, pattern_Code, pattern_Texture);
	}
}

void LongPatterns()
{
	int16_t patternTexture;
	int16_t x, y;
	
	GetPatternTexture(patternTexture);
	GetAbsCoords(x, y);

	// Draw pattern here
	//vectorPattern(x, y, pic_color, pic_priority, pic_control, pattern_Code, pattern_Texture);
	
	while(!IsInstruction(PeekByte()))
	{
		GetPatternTexture(patternTexture);
		GetAbsCoords(x, y);
		//vectorPattern(x, y, pic_color, pic_priority, pic_control, pattern_Code, pattern_Texture);
	}
}

void MediumRelativeLines()
{
	int16_t x, y;
	int16_t oldx, oldy;
	
	GetAbsCoords(x, y);
	
	//WriteByte(0xf6);
	//WriteAgiCoords(x, y);

	while(!IsInstruction(PeekByte()))
	{
		oldx = x;
		oldy = y;
		
		GetRelCoordsMed(x, y);
		
		//WriteAgiCoords(x, y);
		
		if(verbose)
			printf("Line %d, %d - %d, %d\n", oldx, oldy, x, y);
		
		DrawLine(oldx, oldy, x, y);
	}
}

void LongLines()
{
	int16_t x, y;
	int16_t oldx, oldy;
	
	GetAbsCoords(x, y);

	//WriteByte(0xf6);
	//WriteAgiCoords(x, y);

	while(!IsInstruction(PeekByte()))
	{
		oldx = x;
		oldy = y;
		
		GetAbsCoords(x, y);
		
		//WriteAgiCoords(x, y);

		if(verbose)
			printf("Line %d, %d - %d, %d\n", oldx, oldy, x, y);
		
		DrawLine(oldx, oldy, x, y);
	}
}

void ShortRelativeLines()
{
	int16_t x, y;
	int16_t oldx, oldy;
	
	GetAbsCoords(x, y);
	
	//WriteByte(0xf6);
	//WriteAgiCoords(x, y);
	
	while(!IsInstruction(PeekByte()))
	{
		oldx = x;
		oldy = y;
		
		GetRelCoords(x, y);
		
		//WriteAgiCoords(x, y);

		if(verbose)
			printf("Line %d, %d - %d, %d\n", oldx, oldy, x, y);
		
		DrawLine(oldx, oldy, x, y);
	}
}

bool CheckFilledCorrectly(vector<Coord>& agiFilled, vector<Coord>& sciFilled, Coord& failedCoord)
{
	for(Coord& a : agiFilled)
	{
		bool hasMatch = false;
		
		for(Coord& s : sciFilled)
		{
			if(SCI_TO_AGI_X(s.x) == a.x && SCI_TO_AGI_Y(s.y) == a.y)
			{
				hasMatch = true;
				break;
			}
		}
		
		if(!hasMatch)
		{
			failedCoord = a;
			return false;
		}
	}
	
	return true;
}	

void TryFill(Canvas& agiCanvas, Canvas& sciCanvas, vector<Coord>& sciFilled, int16_t x, int16_t y, uint8_t fillColour, bool isVisual, vector<Coord>& agiFilled)
{
	
	while(1)
	{
		agiCanvas.Fill(SCI_TO_AGI_X(x), SCI_TO_AGI_Y(y), fillColour, &agiFilled);
		
		Coord errorCoord;
		if(CheckFilledCorrectly(agiFilled, sciFilled, errorCoord))
		{
			break;
		}

		agiCanvas.UnFill(agiFilled);
		
		uint8_t gapPixel = sciCanvas.GetPixel(AGI_TO_SCI_X(errorCoord.x), AGI_TO_SCI_Y(errorCoord.y));
		if(gapPixel == agiCanvas.defaultColour)
		{
			gapPixel = sciCanvas.GetPixel(AGI_TO_SCI_X(errorCoord.x) + 1, AGI_TO_SCI_Y(errorCoord.y));
		}
		if(gapPixel == agiCanvas.defaultColour)
		{
			gapPixel = 0;
		}
		agiCanvas.SetPixel(errorCoord.x, errorCoord.y, gapPixel);
		
		if(isVisual)
		{
			EmitAgiPixel(errorCoord.x, errorCoord.y, gapPixel, COLOUR_DISABLED);
		}
		else
		{
			EmitAgiPixel(errorCoord.x, errorCoord.y, COLOUR_DISABLED, gapPixel);
		}
	}
}

void DoFill(int16_t x, int16_t y)
{		
	uint8_t fillColour = penColour;
	uint8_t fillPriority = priorityColour;

	vector<Coord> sciVisualFilled, sciPriorityFilled;
	vector<Coord> agiVisualFilled, agiPriorityFilled;

	if(fillColour != COLOUR_DISABLED)
	{
		sciVisualCanvas.Fill(x, y, fillColour, &sciVisualFilled);
	}
	if(priorityColour != COLOUR_DISABLED)
	{
		sciPriorityCanvas.Fill(x, y, fillPriority, &sciPriorityFilled);
	}
	
	// Find and fill gaps
	if(fillColour != COLOUR_DISABLED)
	{
		TryFill(agiVisualCanvas, sciVisualCanvas, sciVisualFilled, x, y, fillColour, true, agiVisualFilled);
	}
	if(fillPriority != COLOUR_DISABLED)
	{
		TryFill(agiPriorityCanvas, sciPriorityCanvas, sciPriorityFilled, x, y, fillPriority, false, agiPriorityFilled);
	}

	EmitAgiSetVisual(fillColour);
	EmitAgiSetPriority(fillPriority);
	EmitAgiFill(SCI_TO_AGI_X(x), SCI_TO_AGI_Y(y));
	
	// Check everywhere is filled correctly
	for(Coord& c : sciVisualFilled)
	{
		if(agiVisualCanvas.GetPixel(SCI_TO_AGI_X(c.x), SCI_TO_AGI_Y(c.y)) == agiVisualCanvas.defaultColour)
		{
			TryFill(agiVisualCanvas, sciVisualCanvas, sciVisualFilled, c.x, c.y, fillColour, true, agiVisualFilled);
			EmitAgiSetVisual(fillColour);
			EmitAgiSetPriority(COLOUR_DISABLED);
			EmitAgiFill(SCI_TO_AGI_X(c.x), SCI_TO_AGI_Y(c.y));
		}
	}
	
	for(Coord& c : sciPriorityFilled)
	{
		if(agiPriorityCanvas.GetPixel(SCI_TO_AGI_X(c.x), SCI_TO_AGI_Y(c.y)) == agiPriorityCanvas.defaultColour)
		{
			TryFill(agiPriorityCanvas, sciPriorityCanvas, sciPriorityFilled, c.x, c.y, fillPriority, true, agiPriorityFilled);
			EmitAgiSetVisual(COLOUR_DISABLED);
			EmitAgiSetPriority(fillPriority);
			EmitAgiFill(SCI_TO_AGI_X(c.x), SCI_TO_AGI_Y(c.y));
		}
	}
	
	EmitAgiSetVisual(fillColour);
	EmitAgiSetPriority(fillPriority);
}

void FloodFill()
{
	int16_t x, y;
			
	while(!IsInstruction(PeekByte()))
	{
		GetAbsCoords(x, y);
		
		DoFill(x, y);

		if(verbose)
			printf("Fill %d, %d\n", x, y);
	}
}

void SetPattern()
{
	patternCode = NextByte();
	
	printf("Set pattern: %d\n", patternCode);
}

void CommandExtensions()
{
	uint8_t commandNumber = NextByte();
	
	switch(commandNumber)
	{
		case 0:		// Set palette entry
		while(!IsInstruction(PeekByte()))
		{
			uint8_t pixel = NextByte();
			uint8_t value = NextByte();
			if(verbose)
				printf("Set palette %d to %x\n", pixel, value);
		}
		break;
		
		case 1:		// Set whole palette
		{
			uint8_t pixel = NextByte();
			for(int c = 0; c < PIC_EGAPALETTE_SIZE; c++)
			{
				NextByte();
			}
		}
		break;
		
		case 2:		// Set monochrome palette
		{
			pictureDataPtr += 41;
		}
		break;
		
		case 3:
		case 4:
		{
			NextByte();
		}
		break;
		
		case 5:
		case 6:
		{
			
		}
		break;
		
		default:
		printf("Uknown command extension: %x\n", commandNumber);
		exit(1);
		break;
	}
}

int main(int argc, char* argv[])
{
	if(argc != 2)
	{
		printf("Usage: %s [pic file]\n", argv[0]);
		return 1;
	}
	
	FILE* fileStream = fopen(argv[1], "rb");
	if(!fileStream)
	{
		printf("Could not open %s\n", argv[1]);
		return 1;
	}
	
	fseek(fileStream, 0, SEEK_END);
	pictureDataLength = ftell(fileStream);
	fseek(fileStream, 0, SEEK_SET);
	pictureData = new uint8_t[pictureDataLength];
	fread(pictureData, pictureDataLength, 1, fileStream);
	fclose(fileStream);

	outputFile = fopen("output.pic", "wb");
	if(!outputFile)
	{
		printf("Could not open file for write\n");
		return 1;
	}
	
	pictureDataPtr = pictureData;
	
	if(NextWord() != 0x0081)
	{
		printf("Incorrect header: not a SCI picture resource?\n");
		return 1;
	}
	
	bool parsing = true;

	while(parsing && pictureDataPtr < pictureData + pictureDataLength)
	{
		uint8_t instruction = NextByte();
		
		switch((int)instruction)
		{
			case 0xf0:
			SetVisualColour();
			break;
			case 0xf1:
			DisableVisual();
			break;
			case 0xf2:
			SetPriorityColour();
			break;
			case 0xf3:
			DisablePriority();
			break;
			case 0xf4:
			ShortPatterns();
			break;
			case 0xf5:
			MediumRelativeLines();
			break;
			case 0xf6:
			LongLines();
			break;
			case 0xf7:
			ShortRelativeLines();
			break;
			case 0xf8:
			FloodFill();
			break;
			case 0xf9:
			SetPattern();
			break;
			case 0xfa:
			LongPatterns();
			break;
			case 0xfb:
			SetControlColour();
			break;
			case 0xfc:
			DisableControl();
			break;
			case 0xfd:
			MediumRelativePatterns();
			break;
			case 0xfe:
			CommandExtensions();
			break;
			case 0xff:
			parsing = false;
			break;
			
			default:
			printf("Unknown instruction! %x\n", instruction);
			parsing = false;
			break;
		}
	}

	WriteByte(0xff);

	fclose(outputFile);

	sciVisualCanvas.DumpToPNG("sci-visual.png");
	agiVisualCanvas.DumpToPNG("agi-visual.png");
	sciPriorityCanvas.DumpToPNG("sci-priority.png");
	agiPriorityCanvas.DumpToPNG("agi-priority.png");

	return 0;
}

int round(float aNumber, float dirn)
{
   if (dirn < 0)
      return ((aNumber - floor(aNumber) <= 0.501)? floor(aNumber) : ceil(aNumber));
   return ((aNumber - floor(aNumber) < 0.499)? floor(aNumber) : ceil(aNumber));
}

Canvas::Canvas(int inWidth, int inHeight, uint8_t inDefaultColour) : width(inWidth), height(inHeight), defaultColour(inDefaultColour)
{
	data = new uint8_t[width * height];
	for(int n = 0; n < width * height; n++)
	{
		data[n] = defaultColour;
	}
}
	
Canvas::~Canvas()
{
	delete[] data;
}

void Canvas::SetPixel(int x, int y, uint8_t value)
{
	if(x >= 0 && y >= 0 && x < width && y < height)
	{
		data[y * width + x] = value;
	}
}

uint8_t Canvas::GetPixel(int x, int y)
{
	if(x >= 0 && y >= 0 && x < width && y < height)
	{
		return data[y * width + x];
	}
	return 0;
}
	
void Canvas::DrawLine(int x1, int y1, int x2, int y2, uint8_t colour)
{
   int height, width, startX, startY;
   float x, y, addX, addY;

   height = (y2 - y1);
   width = (x2 - x1);
   addX = (height==0?height:(float)width/abs(height));
   addY = (width==0?width:(float)height/abs(width));

   if (abs(width) > abs(height)) 
   {
      y = y1;
      addX = (width == 0? 0 : (width/abs(width)));
      for (x=x1; x!=x2; x+=addX) 
	  {
		SetPixel(round(x, addX), round(y, addY), colour);
		y += addY;
      }
      SetPixel(x2, y2, colour);
   }
   else 
   {
      x = x1;
      addY = (height == 0? 0 : (height/abs(height)));
      for (y=y1; y!=y2; y+=addY) 
	  {
		SetPixel(round(x, addX), round(y, addY), colour);
		x+=addX;
      }
      SetPixel(x2,y2, colour);
   }

}

bool Canvas::OkToFill(int x, int y)
{
	return GetPixel(x, y) == defaultColour;
}

void Canvas::UnFill(vector<Coord>& filled)
{
	for(Coord& c : filled)
	{
		SetPixel(c.x, c.y, defaultColour);
	}
}

void Canvas::Fill(int x, int y, uint8_t colour, vector<Coord>* filled)
{
	vector<Coord> queue;

	if (colour == defaultColour)
		return;
	
	queue.push_back(Coord(x, y));
	
	if(filled)
	{
		filled->clear();
	}
	
	while(queue.size() > 0) 
	{
		Coord coord = queue[0];
		queue.erase(queue.begin());

		if (OkToFill(coord.x, coord.y)) 
		{
			SetPixel(coord.x, coord.y, colour);
			
			if(filled)
			{
				filled->push_back(coord);
			}

			if ((coord.x != 0) && OkToFill(coord.x - 1, coord.y)) 
			{
				queue.push_back(Coord(coord.x - 1, coord.y));
			}
			if ((coord.y != 0) && OkToFill(coord.x, coord.y - 1)) 
			{
				queue.push_back(Coord(coord.x, coord.y - 1));
			}
			if ((coord.x + 1 < width) && OkToFill(coord.x + 1, coord.y)) 
			{
				queue.push_back(Coord(coord.x + 1, coord.y));
			}
			if ((coord.y + 1 < height) && OkToFill(coord.x, coord.y + 1)) 
			{
				queue.push_back(Coord(coord.x, coord.y + 1));
			}
		}
	}
}
	
void Canvas::DumpToPNG(const char* filename)
{
	vector<uint8_t> outputData;
	
	for(int n = 0; n < width * height; n++)
	{
		int index = data[n];
		if(index >= 16)
		{
			index = 0;
		}
		outputData.push_back(EGAPalette[index * 3]);
		outputData.push_back(EGAPalette[index * 3 + 1]);
		outputData.push_back(EGAPalette[index * 3 + 2]);
		outputData.push_back(0xff);
	}
	
	lodepng::encode(filename, outputData, width, height);
}
	
	