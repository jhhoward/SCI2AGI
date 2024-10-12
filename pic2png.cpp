/**************************************************************************
** agi-upscale.cpp
**
** Upscaler for AGI PIC resources. Based on showpic.c
**
**************************************************************************/

#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <stdint.h>
#include <vector>
#include "lodepng.cpp"

#define BASE_WIDTH 160
#define BASE_HEIGHT 168

// Change these values to upscale to a different size:
//#define UPSCALED_WIDTH 320
//#define UPSCALED_HEIGHT 168
#define UPSCALED_WIDTH 160
#define UPSCALED_HEIGHT 96

typedef unsigned char byte;
typedef unsigned short int word;

struct Bitmap
{
	Bitmap(unsigned int inWidth, unsigned int inHeight, uint8_t inClearColour) : width(inWidth), height(inHeight), clearColour(inClearColour)
	{
		data = new uint8_t[width * height];
		memset(data, clearColour, width * height);
	}
	~Bitmap()
	{
		delete[] data;
	}

	void Set(int x, int y, uint8_t col)
	{
		if (x >= 0 && y >= 0 && x < width && y < height)
		{
			data[y * width + x] = col;
		}
	}

	uint8_t Get(int x, int y)
	{
		if (x >= 0 && y >= 0 && x < width && y < height)
		{
			return data[y * width + x];
		}
		return clearColour;
	}

	unsigned int width, height;
	uint8_t* data;
	uint8_t clearColour;
};

/* QUEUE DEFINITIONS */

#define QMAX 8000
#define EMPTY 0xFFFF

class PicDrawer
{
public:
	PicDrawer(unsigned int width, unsigned int height);
	~PicDrawer();

	void setReferenceDrawer(PicDrawer* inReferenceDrawer) { referenceDrawer = inReferenceDrawer; }
	void beginDrawing(uint8_t* inData, unsigned length);
	bool drawStep();
	void fillGaps();

	Bitmap* getPicture() { return picture; }

private:
	void scaleCoordinates(word& x, word& y);
	uint8_t getReferencePicture(word x, word y);
	uint8_t getReferencePriority(word x, word y);

	void qstore(word q);
	word qretrieve();
	void pset(word x, word y);
	int round(float aNumber, float dirn);
	void drawline(word x1, word y1, word x2, word y2);
	bool okToFill(word x, word y);
	void agiFill(word x, word y);

	void xCorner(byte** data);
	void yCorner(byte** data);
	void relativeDraw(byte** data);
	void fill(byte** data);
	void absoluteLine(byte** data);
	void plotPattern(byte x, byte y);
	void plotBrush(byte** data);

	bool didFill(word x, word y);
	bool didReferenceFill(word x, word y);

	PicDrawer* referenceDrawer = nullptr;

	uint8_t* pictureData;
	uint8_t* pictureDataPtr;
	unsigned pictureDataLength;
	bool isDrawing;

	Bitmap* picture;
	Bitmap* priority;

	bool picDrawEnabled = false, priDrawEnabled = false;
	byte picColour = 0, priColour = 0, patCode, patNum;

	uint8_t* lastFill;

	word buf[QMAX + 1];
	int rpos = QMAX, spos = 0;

	float picScaleX, picScaleY;
};

void PicDrawer::qstore(word q)
{
   if (spos+1==rpos || (spos+1==QMAX && !rpos)) {
      return;
   }
   buf[spos] = q;
   spos++;
   if (spos==QMAX) spos = 0;  /* loop back */
}

word PicDrawer::qretrieve()
{
   if (rpos==QMAX) rpos=0;  /* loop back */
   if (rpos==spos) {
      return EMPTY;
   }
   rpos++;
   return buf[rpos-1];
}

void PicDrawer::scaleCoordinates(word& x, word& y)
{
	if(x == 159)
	{
		x = picture->width - 1;
	}
	else
	{
		x = (word)(x * picScaleX);
	}
	y = (word)(y * picScaleY);
}

/**************************************************************************
** pset
**
** Draws a pixel in each screen depending on whether drawing in that
** screen is enabled or not.
**************************************************************************/
void PicDrawer::pset(word x, word y)
{
   if (picDrawEnabled) picture->Set(x, y, picColour);
   if (priDrawEnabled) priority->Set(x, y, priColour);
}

/**************************************************************************
** round
**
** Rounds a float to the closest int. Takes into actions which direction
** the current line is being drawn when it has a 50:50 decision about
** where to put a pixel.
**************************************************************************/
int PicDrawer::round(float aNumber, float dirn)
{
   if (dirn < 0)
      return ((aNumber - floor(aNumber) <= 0.501)? floor(aNumber) : ceil(aNumber));
   return ((aNumber - floor(aNumber) < 0.499)? floor(aNumber) : ceil(aNumber));
}

/**************************************************************************
** drawline
**
** Draws an AGI line.
**************************************************************************/
void PicDrawer::drawline(word x1, word y1, word x2, word y2)
{
	scaleCoordinates(x1, y1);
	scaleCoordinates(x2, y2);

   int height, width, startX, startY;
   float x, y, addX, addY;

   height = (y2 - y1);
   width = (x2 - x1);
   addX = (height==0?height:(float)width/abs(height));
   addY = (width==0?width:(float)height/abs(width));

   if (abs(width) > abs(height)) {
      y = y1;
      addX = (width == 0? 0 : (width/abs(width)));
      for (x=x1; x!=x2; x+=addX) {
	 pset(round(x, addX), round(y, addY));
	 y+=addY;
      }
      pset(x2,y2);
   }
   else {
      x = x1;
      addY = (height == 0? 0 : (height/abs(height)));
      for (y=y1; y!=y2; y+=addY) {
	 pset(round(x, addX), round(y, addY));
	 x+=addX;
      }
      pset(x2,y2);
   }

}

/**************************************************************************
** okToFill
**************************************************************************/
bool PicDrawer::okToFill(word x, word y)
{
   if (!picDrawEnabled && !priDrawEnabled) return false;
   if (picColour == 15) return false;
   if (!priDrawEnabled)
   {
	   if (picture->Get(x, y) != 15)
		   return false;

	   if (referenceDrawer)
	   {
		   /*
		   bool matches = false;
		   for (int i = -1; i <= 1; i++)
		   {
			   for (int j = -1; j <= 1; j++)
			   {
				   matches = matches || (getReferencePicture(x + i, y + j) == picColour);
			   }
		   }
		   if (!matches)
			   return false;*/
		   if (!didReferenceFill(x, y))
			   return false;
	   }

	   return true;
   }
   if (priDrawEnabled && !picDrawEnabled)
   {
	   return (priority->Get(x, y) == 4);
   }

   if (picture->Get(x, y) != 15)
	   return false;

   if (referenceDrawer)
   {
	   /*
	   bool matches = false;
	   for (int i = -1; i <= 1; i++)
	   {
		   for (int j = -1; j <= 1; j++)
		   {
			   matches = matches || (getReferencePicture(x + i, y + j) == picColour);
		   }
	   }
	   if (!matches)
		   return false;*/
	   //if (!referenceDrawer->didFill((word)(x / picScaleX), (word)(y / picScaleY)))
		 //  return false;
	   if (!didReferenceFill(x, y))
		   return false;
   }

   return true;
}

/**************************************************************************
** agiFill
**************************************************************************/
void PicDrawer::agiFill(word x, word y)
{
   word x1, y1;
   rpos = spos = 0;

   scaleCoordinates(x, y);

	if(referenceDrawer)
	{
		for(int j = 0; j < picture->height; j++)
		{
			for(int i = 0; i < picture->width; i++)
			{
				if(!okToFill(i, j))
					continue;
				
				word refX = (word)(i / picScaleX);
				word refY = (word)(j / picScaleY);
				
				if(referenceDrawer->lastFill[refY * referenceDrawer->picture->width + refX])
				{
					pset(i, j);
					lastFill[j * picture->width + i] = true;
				}
			}
		}
		
		bool* nextFill = new bool[picture->width * picture->height];
		memset(nextFill, 0, picture->width * picture->height * sizeof(bool));
		for(int k = 0; k < 1; k++)
		{
			for(int j = 0; j < picture->height; j++)
			{
				for(int i = 0; i < picture->width; i++)
				{
					if(!okToFill(i, j))
						continue;

					if((i > 0 && lastFill[j * picture->width + i - 1])
					|| (i < picture->width - 1 && lastFill[j * picture->width + i + 1])
					|| (j < picture->height - 1 && lastFill[(j + 1) * picture->width + i])
					|| (j > 0 && lastFill[(j - 1) * picture->width + i]))
					{
						pset(i, j);
						nextFill[j * picture->width + i] = true;
					}
				}
			}
			
			for(int i = 0; i < picture->width * picture->height; i++)
			{
				lastFill[i] = lastFill[i] || nextFill[i];
			}
		}
		
		delete[] nextFill;
		
		return;
	}

   //if (referenceDrawer)
//	   return;

   qstore(x);
   qstore(y);

   for (;;) {

      x1 = qretrieve();
      y1 = qretrieve();

      if ((x1 == EMPTY) || (y1 == EMPTY))
	 break;
      else {

	 if (okToFill(x1,y1)) {

	    pset(x1, y1);
		lastFill[y1 * picture->width + x1] = 1;

	    if (okToFill(x1, y1-1) && (y1!=0)) {
	       qstore(x1);
	       qstore(y1-1);
	    }
	    if (okToFill(x1-1, y1) && (x1!=0)) {
	       qstore(x1-1);
	       qstore(y1);
	    }
	    if (okToFill(x1+1, y1) && (x1!=picture->width - 1)) {
	       qstore(x1+1);
	       qstore(y1);
	    }
	    if (okToFill(x1, y1+1) && (y1!=picture->height - 1)) {
	       qstore(x1);
	       qstore(y1+1);
	    }

	 }

      }

   }

}

/**************************************************************************
** xCorner
**
** Draws an xCorner  (drawing action 0xF5)
**************************************************************************/
void PicDrawer::xCorner(byte **data)
{
   byte x1, x2, y1, y2;

   x1 = *((*data)++);
   y1 = *((*data)++);

//   scaleCoordinates(x1, y1);

   pset((word)(picScaleX * x1),(word)(picScaleY * y1));

   for (;;) {
      x2 = *((*data)++);
	  //x2 = (word)(x2 * picScaleX);
      if (x2 >= 0xF0) break;
      drawline(x1, y1, x2, y1);
      x1 = x2;
      y2 = *((*data)++);
	  //y2 = (word)(y2 * picScaleX);
      if (y2 >= 0xF0) break;
      drawline(x1, y1, x1, y2);
      y1 = y2;
   }

   (*data)--;
}

/**************************************************************************
** yCorner
**
** Draws an yCorner  (drawing action 0xF4)
**************************************************************************/
void PicDrawer::yCorner(byte **data)
{
   byte x1, x2, y1, y2;

   x1 = *((*data)++);
   y1 = *((*data)++);

   //scaleCoordinates(x1, y1);
   pset((word)(picScaleX * x1), (word)(picScaleY * y1));

   for (;;) {
      y2 = *((*data)++);
	  //y2 = (word)(y2 * picScaleX);
	  if (y2 >= 0xF0) break;
      drawline(x1, y1, x1, y2);
      y1 = y2;
      x2 = *((*data)++);
	  //x2 = (word)(x2 * picScaleX);
      if (x2 >= 0xF0) break;
      drawline(x1, y1, x2, y1);
      x1 = x2;
   }

   (*data)--;
}

/**************************************************************************
** relativeDraw
**
** Draws short lines relative to last position.  (drawing action 0xF7)
**************************************************************************/
void PicDrawer::relativeDraw(byte **data)
{
   word x1, y1, disp;
   char dx, dy;

   x1 = *((*data)++);
   y1 = *((*data)++);
   
   pset((word)(picScaleX * x1), (word)(picScaleY * y1));

   for (;;) {
      disp = *((*data)++);
      if (disp >= 0xF0) break;
      dx = ((disp & 0xF0) >> 4) & 0x0F;
      dy = (disp & 0x0F);
      if (dx & 0x08) dx = (-1)*(dx & 0x07);
      if (dy & 0x08) dy = (-1)*(dy & 0x07);
      drawline(x1, y1, x1 + dx, y1 + dy);
      x1 += dx;
      y1 += dy;
   }

   (*data)--;
}

/**************************************************************************
** fill
**
** Agi flood fill.  (drawing action 0xF8)
**************************************************************************/
void PicDrawer::fill(byte **data)
{
	memset(lastFill, 0, picture->width * picture->height);

   byte x1, y1;

   for (;;) {
      if ((x1 = *((*data)++)) >= 0xF0) break;
      if ((y1 = *((*data)++)) >= 0xF0) break;
      agiFill(x1, y1);
   }

   (*data)--;
}

/**************************************************************************
** absoluteLine
**
** Draws long lines to actual locations (cf. relative) (drawing action 0xF6)
**************************************************************************/
void PicDrawer::absoluteLine(byte **data)
{
   word x1, y1, x2, y2;

   x1 = *((*data)++);
   y1 = *((*data)++);

   pset((word)(picScaleX * x1), (word)(picScaleY * y1));

   for (;;) {
      if ((x2 = *((*data)++)) >= 0xF0) break;
      if ((y2 = *((*data)++)) >= 0xF0) break;
      drawline(x1, y1, x2, y2);
      x1 = x2;
      y1 = y2;
   }

   (*data)--;
}


#define plotPatternPoint() \
   if (patCode & 0x20) { \
      if ((splatterMap[bitPos>>3] >> (7-(bitPos&7))) & 1) pset((word)(x1 * picScaleX), (word)(y1 * picScaleY)); \
      bitPos++; \
      if (bitPos == 0xff) bitPos=0; \
   } else pset((word)(x1 * picScaleX), (word)(y1 * picScaleY))

/**************************************************************************
** plotPattern
**
** Draws pixels, circles, squares, or splatter brush patterns depending
** on the pattern code.
**************************************************************************/
void PicDrawer::plotPattern(byte x, byte y)
{ 
  static int8_t circles[][15] = { /* agi circle bitmaps */
    {0x80},
    {0xfc},
    {0x5f, 0xf4},
    {0x66, 0xff, 0xf6, 0x60},
    {0x23, 0xbf, 0xff, 0xff, 0xee, 0x20},
    {0x31, 0xe7, 0x9e, 0xff, 0xff, 0xde, 0x79, 0xe3, 0x00},
    {0x38, 0xf9, 0xf3, 0xef, 0xff, 0xff, 0xff, 0xfe, 0xf9, 0xf3, 0xe3, 0x80},
    {0x18, 0x3c, 0x7e, 0x7e, 0x7e, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7e, 0x7e,
     0x7e, 0x3c, 0x18}
  };

  static byte splatterMap[32] = { /* splatter brush bitmaps */
    0x20, 0x94, 0x02, 0x24, 0x90, 0x82, 0xa4, 0xa2,
    0x82, 0x09, 0x0a, 0x22, 0x12, 0x10, 0x42, 0x14,
    0x91, 0x4a, 0x91, 0x11, 0x08, 0x12, 0x25, 0x10,
    0x22, 0xa8, 0x14, 0x24, 0x00, 0x50, 0x24, 0x04
  };

  static byte splatterStart[128] = { /* starting bit position */
    0x00, 0x18, 0x30, 0xc4, 0xdc, 0x65, 0xeb, 0x48,
    0x60, 0xbd, 0x89, 0x05, 0x0a, 0xf4, 0x7d, 0x7d,
    0x85, 0xb0, 0x8e, 0x95, 0x1f, 0x22, 0x0d, 0xdf,
    0x2a, 0x78, 0xd5, 0x73, 0x1c, 0xb4, 0x40, 0xa1,
    0xb9, 0x3c, 0xca, 0x58, 0x92, 0x34, 0xcc, 0xce,
    0xd7, 0x42, 0x90, 0x0f, 0x8b, 0x7f, 0x32, 0xed,
    0x5c, 0x9d, 0xc8, 0x99, 0xad, 0x4e, 0x56, 0xa6,
    0xf7, 0x68, 0xb7, 0x25, 0x82, 0x37, 0x3a, 0x51,
    0x69, 0x26, 0x38, 0x52, 0x9e, 0x9a, 0x4f, 0xa7,
    0x43, 0x10, 0x80, 0xee, 0x3d, 0x59, 0x35, 0xcf,
    0x79, 0x74, 0xb5, 0xa2, 0xb1, 0x96, 0x23, 0xe0,
    0xbe, 0x05, 0xf5, 0x6e, 0x19, 0xc5, 0x66, 0x49,
    0xf0, 0xd1, 0x54, 0xa9, 0x70, 0x4b, 0xa4, 0xe2,
    0xe6, 0xe5, 0xab, 0xe4, 0xd2, 0xaa, 0x4c, 0xe3,
    0x06, 0x6f, 0xc6, 0x4a, 0xa4, 0x75, 0x97, 0xe1
  };

  int circlePos = 0;
  byte x1, y1, penSize, bitPos = splatterStart[patNum];

  penSize = (patCode&7);

  if (x<((penSize/2)+1)) x=((penSize/2)+1);
  else if (x>160-((penSize/2)+1)) x=160-((penSize/2)+1);
  if (y<penSize) y = penSize;
  else if (y>=168-penSize) y=167-penSize;

  for (y1=y-penSize; y1<=y+penSize; y1++) {
    for (x1=x-(ceil((float)penSize/2)); x1<=x+(floor((float)penSize/2)); x1++) {
      if (patCode & 0x10) { /* Square */
	plotPatternPoint();
      }
      else { /* Circle */
	if ((circles[patCode&7][circlePos>>3] >> (7-(circlePos&7)))&1) {
	  plotPatternPoint();
	}
	circlePos++;
      }
    }
  }

} 


/**************************************************************************
** plotBrush
**
** Plots points and various brush patterns.
**************************************************************************/
void PicDrawer::plotBrush(byte **data)
{
   byte x1, y1, store;

   for (;;) {
     if (patCode & 0x20) {
	if ((patNum = *((*data)++)) >= 0xF0) break;
	patNum = (patNum >> 1 & 0x7f);
     }
     if ((x1 = *((*data)++)) >= 0xF0) break;
     if ((y1 = *((*data)++)) >= 0xF0) break;
     plotPattern(x1, y1);
   }

   (*data)--;
}

/**************************************************************************
** getLength
**
** Return the length of the given file.
**************************************************************************/
long getLength(FILE *file)
{
	long tmp;

	fseek(file, 0L, SEEK_END);
	tmp = ftell(file);
	fseek(file, 0L, SEEK_SET);

	return(tmp);
}

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

uint8_t EGAPaletteGrey[] = 
{
	0x00, 0x00, 0x00,
	0x55, 0x55, 0x55,
	0x55, 0x55, 0x55,
	0x55, 0x55, 0x55,
	0x55, 0x55, 0x55,
	0x55, 0x55, 0x55,
	0x55, 0x55, 0x55,
	0xaa, 0xaa, 0xaa,
	0x55, 0x55, 0x55,
	0xaa, 0xaa, 0xaa,
	0xaa, 0xaa, 0xaa,
	0xff, 0xff, 0xff,
	0xaa, 0xaa, 0xaa,
	0xff, 0xff, 0xff,
	0xff, 0xff, 0xff,
	0xff, 0xff, 0xff
};

void DumpToPNG(Bitmap* pic, const char* path)
{
	std::vector<uint8_t> data;
	uint8_t* palette = EGAPalette;

	for(unsigned int y = 0; y < pic->height; y++)
	{
		for(unsigned int x = 0; x < pic->width; x++)
		{
			int index = pic->data[y * pic->width + x];
			data.push_back(palette[index * 3]);
			data.push_back(palette[index * 3 + 1]);
			data.push_back(palette[index * 3 + 2]);
			data.push_back(0xff);
		}
	}
	
	lodepng::encode(path, data, pic->width, pic->height);
}

uint8_t PicDrawer::getReferencePicture(word x, word y)
{
	return referenceDrawer->picture->Get((word)(x / picScaleX), (word)(y / picScaleY));
}

uint8_t PicDrawer::getReferencePriority(word x, word y)
{
	return referenceDrawer->priority->Get((word)(x / picScaleX), (word)(y / picScaleY));
}

PicDrawer::PicDrawer(unsigned int width, unsigned int height)
{
	picScaleX = (float)width / 160.0f;
	picScaleY = (float)height / 168.0f;

	picture = new Bitmap(width, height, 15);
	priority = new Bitmap(width, height, 4);

	lastFill = new byte[width * height];
}

PicDrawer::~PicDrawer()
{
	delete picture;
	delete priority;
	delete[] lastFill;
}

bool PicDrawer::didFill(word x, word y)
{
	if (x < 0 || y < 0 || x >= picture->width || y >= picture->height)
	{
		return false;
	}
	return lastFill[y * picture->width + x] != 0;
}

bool PicDrawer::didReferenceFill(word x, word y)
{
	word scaledX = (word)(x / picScaleX);
	word scaledY = (word)(y / picScaleY);

	//return referenceDrawer->didFill(scaledX, scaledY);
	
	for (int j = -1; j <= 1; j++)
	{
		for (int i = -1; i <= 1; i++)
		{
			if (referenceDrawer->didFill(scaledX + i, scaledY + j))
			{
				return true;
			}
		}
	}

	return false;
}

void PicDrawer::beginDrawing(uint8_t* inData, unsigned length)
{
	pictureDataPtr = pictureData = inData;
	pictureDataLength = length;
	isDrawing = true;
}

bool PicDrawer::drawStep()
{
	if (!isDrawing)
	{
		return false;
	}

	uint8_t action = *(pictureDataPtr++);

	switch (action) {
	case 0xFF: isDrawing = false; break;
	case 0xF0: picColour = *(pictureDataPtr++);
		picDrawEnabled = true;
		break;
	case 0xF1: picDrawEnabled = false; break;
	case 0xF2: priColour = *(pictureDataPtr++);
		priDrawEnabled = true;
		break;
	case 0xF3: priDrawEnabled = false; break;
	case 0xF4: yCorner(&pictureDataPtr); break;
	case 0xF5: xCorner(&pictureDataPtr); break;
	case 0xF6: absoluteLine(&pictureDataPtr); break;
	case 0xF7: relativeDraw(&pictureDataPtr); break;
	case 0xF8: fill(&pictureDataPtr); break;
	case 0xF9: patCode = *(pictureDataPtr++); break;
	case 0xFA: plotBrush(&pictureDataPtr); break;
	default: printf("Unknown picture code : %X width: %d, height: %d\n", action, picture->width, picture->height); exit(0);
	}

	if (pictureDataPtr >= (pictureData + pictureDataLength))
	{
		isDrawing = false;
	}

	return isDrawing;
}

void PicDrawer::fillGaps()
{
	for (int y = 0; y < picture->height; y++)
	{
		for (int x = 0; x < picture->width; x++)
		{
			if (picture->Get(x, y) == 15)
			{
				int scaledX = (int)(x / picScaleX);
				int scaledY = (int)(y / picScaleY);
				bool refHasWhite = false;

				for (int i = -1; i <= 1; i++)
				{
					if (referenceDrawer->picture->Get(scaledX + i, scaledY) == 15)
					{
						refHasWhite = true;
						break;
					}
				}

				if (!refHasWhite)
				{
					picture->Set(x, y, referenceDrawer->picture->Get(scaledX, scaledY));
				}
			}
		}
	}
}

void processFile(int number)
{
	FILE* pictureFile;
	char filename[20];
	sprintf(filename, "PICTURE.%d", number);
	pictureFile = fopen(filename, "rb");
	if(!pictureFile)
	{
		return;
	}

	long fileLen = getLength(pictureFile);
	uint8_t* dataFile = (byte *)malloc(fileLen + 20);
	fread(dataFile, 1, fileLen, pictureFile);
	fclose(pictureFile);

	PicDrawer baseDrawer(BASE_WIDTH, BASE_HEIGHT);
	PicDrawer upscaleDrawer(UPSCALED_WIDTH, UPSCALED_HEIGHT);
	upscaleDrawer.setReferenceDrawer(&baseDrawer);

	baseDrawer.beginDrawing(dataFile, fileLen);
	upscaleDrawer.beginDrawing(dataFile, fileLen);

	while (baseDrawer.drawStep())
	{
	   upscaleDrawer.drawStep();
	}

	upscaleDrawer.fillGaps();

	sprintf(filename, "upscale-%d.png", number);
	DumpToPNG(upscaleDrawer.getPicture(), filename);

	free(dataFile);
	
}

/**************************************************************************
** MAIN PROGRAM
**************************************************************************/
#if 1
void main(int argc, char* argv[])
{
   FILE *pictureFile;

   if (argc != 2) {
      printf("Usage: %s filename\n", argv[0]);
      exit(0);
   }
   else {
      if ((pictureFile = fopen(argv[1], "rb")) == NULL) {
	      printf("Error opening file : %s\n", argv[1]);
	      exit(0);
      }
   }

   long fileLen = getLength(pictureFile);
   uint8_t* dataFile = (byte *)malloc(fileLen + 20);
   fread(dataFile, 1, fileLen, pictureFile);
   fclose(pictureFile);
   
   PicDrawer baseDrawer(BASE_WIDTH, BASE_HEIGHT);
   baseDrawer.beginDrawing(dataFile, fileLen);
   
   while (baseDrawer.drawStep())
   {
   }

   char outputPath[512];
   snprintf(outputPath, 512, "%s.png", argv[1]);

   DumpToPNG(baseDrawer.getPicture(), outputPath);

   free(dataFile);
}
#else
void main(int argc, char* argv[])
{
   FILE *pictureFile;

   if(argc == 2 && !strcmp(argv[1], "ALL"))
   {
	   for(int n = 0; n < 256; n++)
	   {
		   processFile(n);
	   }
	   return;
   }

   if (argc != 2) {
      printf("Usage: %s filename\n", argv[0]);
      exit(0);
   }
   else {
      if ((pictureFile = fopen(argv[1], "rb")) == NULL) {
	      printf("Error opening file : %s\n", argv[1]);
	      exit(0);
      }
   }

   long fileLen = getLength(pictureFile);
   uint8_t* dataFile = (byte *)malloc(fileLen + 20);
   fread(dataFile, 1, fileLen, pictureFile);
   fclose(pictureFile);
   
   PicDrawer baseDrawer(BASE_WIDTH, BASE_HEIGHT);
   PicDrawer upscaleDrawer(UPSCALED_WIDTH, UPSCALED_HEIGHT);
   upscaleDrawer.setReferenceDrawer(&baseDrawer);

   baseDrawer.beginDrawing(dataFile, fileLen);
   upscaleDrawer.beginDrawing(dataFile, fileLen);
   
   while (baseDrawer.drawStep())
   {
	   upscaleDrawer.drawStep();
   }

   upscaleDrawer.fillGaps();

   DumpToPNG(baseDrawer.getPicture(), "base.png");
   DumpToPNG(upscaleDrawer.getPicture(), "upscale.png");

   free(dataFile);
}
#endif