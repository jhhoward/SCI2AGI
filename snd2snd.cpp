#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <vector>
#include "lodepng.cpp"

#define NUM_INPUT_CHANNELS 16
#define NUM_OUTPUT_CHANNELS 3

using namespace std;
constexpr int tandyMask = 0x1000;
constexpr int pcMask = 0x2000;
bool verbose = false;

#pragma pack(1)
struct SciSoundHeader
{
	uint16_t magicNumber;		// should be 84 00
	uint8_t digitalSampleFlag;
	uint16_t channelData[NUM_INPUT_CHANNELS];
};

struct SciSoundEvent
{
	union
	{
		struct
		{
			uint8_t channel : 4;
			uint8_t command : 4;
		};
		uint8_t raw;
	};
};

int GetFValue(float frequency)
{
	if(frequency <= 0)
		return 0x3ff;
	//return 4770000 / (frequency * 32);
	//int result = (int)(3579540.0f / (frequency * 32));
	//int result = (int)(4770000.0f / (frequency * 32));
	int result = (int)(3579545.0f / (frequency * 32));

	if(result > 0x3ff)
	{
		printf("Warning! Frequency %f is out of range\n", frequency);
		return GetFValue(frequency * 2);
	}
	return result;
}

struct OutputChannel
{
	OutputChannel() : lastTime(0) {}

	// f = 111860 / (((Byte2 & 0x3F) << 4) + (Byte1 & 0x0F))
	// val = 111860 / frequency
	
	void ChangeFrequency(float freq, float time)
	{
		if(freq == frequency)
			return;
		
		float deltaTime = time - lastTime;
		int ticks = (int)(deltaTime / (1.0f / 60.0f));
		
		if(ticks > 0)
		{
			if(verbose)
			{
				if(frequency > 0)
					printf("%d [%d] %f : Note on: %f\n", channelIndex, ticks, lastTime, frequency);
				else
					printf("%d [%d] %f : Note off\n", channelIndex, ticks, lastTime);
			}

			if(frequency > 0)
			{
				//int tone = (int)(111860 / (frequency));
				int tone = GetFValue(frequency);
				//tone &= 0x7ff;
				//if(tone > 0x7ff)
				//	tone = 0x7ff;
				
				//uint8_t param1 = (tone >> 4) | (0x80) | (channelIndex << 4);
				//uint8_t param2 = tone & 0xf;
				uint8_t param1 = tone & 0xf;
				uint8_t param2 = (tone >> 4) & 0x3f;
				
				#if 1
				{
					outputStream.push_back((uint8_t)(ticks & 0xff));
					outputStream.push_back((uint8_t)(ticks >> 8));
					
					outputStream.push_back(param2);
					outputStream.push_back(param1);

					outputStream.push_back(0x00);
				}
				#else
				{
					int activeTicks = ticks - 10;
					if(activeTicks < 1)
						activeTicks = 1;
					int fadeTicks = ticks - activeTicks;

					outputStream.push_back((uint8_t)(activeTicks & 0xff));
					outputStream.push_back((uint8_t)(activeTicks >> 8));
					
					outputStream.push_back(param1);
					outputStream.push_back(param2);

					outputStream.push_back(0x00);
					
					for(int n = 0; n < fadeTicks; n++)
					{
						outputStream.push_back(1);
						outputStream.push_back(0);
						
						outputStream.push_back(param1);
						outputStream.push_back(param2);

						outputStream.push_back(1 + n);
					}
				}
				#endif
			}
			else
			{
				outputStream.push_back((uint8_t)(ticks & 0xff));
				outputStream.push_back((uint8_t)(ticks >> 8));
				outputStream.push_back(0x00);
				outputStream.push_back(0x00);
				outputStream.push_back(0x0f);
			}
		}
		
		frequency = freq;
		lastTime = time;
	}

	void Close()
	{
		outputStream.push_back(0xff);
		outputStream.push_back(0xff);
	}

	vector<uint8_t> outputStream;
	float frequency = 0;
	float lastTime = 0.0f;
	int channelIndex;
	int borrowedTicks = 0;
};

#define BASE_NOTE 129	// A10
#define BASE_OCTAVE 10	// A10, as I said

static const int freq_table[12] = { // A4 is 440Hz, halftone map is x |-> ** 2^(x/12)
	28160, // A10
	29834,
	31608,
	33488,
	35479,
	37589,
	39824,
	42192,
	44701,
	47359,
	50175,
	53159
};

int get_freq(int note) {
	int halftone_delta = note - BASE_NOTE;
	int oct_diff = ((halftone_delta + BASE_OCTAVE * 12) / 12) - BASE_OCTAVE;
	int halftone_index = (halftone_delta + (12 * 100)) % 12;
	int freq = (!note) ? 0 : freq_table[halftone_index] / (1 << (-oct_diff));

	return freq;
}

int channelMapping[NUM_INPUT_CHANNELS];
OutputChannel outputChannels[NUM_OUTPUT_CHANNELS];

int main(int argc, char* argv[])
{
	const char* inputPath = nullptr;
	const char* outputPath = nullptr;
	
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
		outputPath = "output.snd";
	}
	
	FILE* fileStream = fopen(inputPath, "rb");
	if(!fileStream)
	{
		printf("Could not open %s\n", inputPath);
		return 1;
	}
	
	fseek(fileStream, 0, SEEK_END);
	int soundDataLength = ftell(fileStream);
	fseek(fileStream, 0, SEEK_SET);
	uint8_t* soundData = new uint8_t[soundDataLength];
	fread(soundData, soundDataLength, 1, fileStream);
	fclose(fileStream);

	if(soundDataLength < sizeof(SciSoundHeader))
	{
		printf("Invalid sound file\n");
		return 1;
	}
	
	uint8_t* inputPtr = soundData;
	SciSoundHeader* header = (SciSoundHeader*) inputPtr;

	if(header->magicNumber != 0x84)
	{
		printf("Invalid header!\n");
		return 1;
	}
	
	for(int n = 0; n < NUM_OUTPUT_CHANNELS; n++)
	{
		outputChannels[n].channelIndex = n;
	}
	
	int numSecondaryChannels = 0;
	
	for(int n = 0; n < NUM_INPUT_CHANNELS; n++)
	{
		channelMapping[n] = -1;
		
		if(header->channelData[n] & pcMask)
		{
			channelMapping[n] = 0;
		}
		else if((header->channelData[n] & tandyMask) && numSecondaryChannels < NUM_OUTPUT_CHANNELS - 1)
		{
			channelMapping[n] = numSecondaryChannels + 1;
			numSecondaryChannels++;
		}
		else
		{
			channelMapping[n] = -1;
		}
	}

	inputPtr += sizeof(SciSoundHeader);
	
	uint8_t lastEventRaw = 0;
	float time = 0.0f;
	
	while(inputPtr < soundData + soundDataLength)
	{
		uint8_t deltaTime = *inputPtr++;
		
		if(deltaTime == 0xfc)
			break;
		
		time += deltaTime / 60.0f;
		
		SciSoundEvent ev = *(SciSoundEvent*)(inputPtr);
		
		if(ev.raw == 0xfc)
			break;
		
		if(ev.command < 8)
		{
			ev.raw = lastEventRaw;
		}
		else
		{
			inputPtr++;
		}
		lastEventRaw = ev.raw;
		
		uint8_t param1 = 0, param2 = 0;
		
		switch(ev.command)
		{
			default:
			printf("Unknown command: %x (%x)\n", ev.command, ev.raw);
			break;
			break;
			case 0xC:
			case 0xD:
			param1 = *inputPtr++;
			break;
			case 0xB:
			case 0x8:
			case 0x9:
			case 0xA:
			case 0xE:
			param1 = *inputPtr++;
			param2 = *inputPtr++;
			break;
		}
		
		if(ev.command == 0x9)
		{
			// Note on
			if(channelMapping[ev.channel] != -1)
			{
				outputChannels[channelMapping[ev.channel]].ChangeFrequency(param2 ? get_freq(param1) : 0, time);
				//outputChannels[channelMapping[ev.channel]].ChangeFrequency(get_freq(param1), time);
			}
		}
		else if(ev.command == 0x8)
		{
			// Note off
			if(channelMapping[ev.channel] != -1)
			{
				outputChannels[channelMapping[ev.channel]].ChangeFrequency(0, time);
			}
		}
		else if(ev.command == 0xb)
		{
			//printf("Control: %x\n", param1);
			if(param1 == 0x40)
			{
				// Pedal control
				//if(param2)
				//{
				//	printf("Pedal on\n");
				//}
				//else printf("Pedal off\n");
			}	
			if(param1 == 0x7B || param1 == 0x78)
			{
				if(channelMapping[ev.channel] != -1)
				{
					if(verbose)
						printf("Control sound off\n");
					outputChannels[channelMapping[ev.channel]].ChangeFrequency(0, time);
				}
			}
		}
	}
	
	if(verbose)
	{
		printf("Total time: %f seconds\n", time);
	}
	
	FILE* outputFile = fopen(outputPath, "wb");
	if(!outputFile)
	{
		printf("Could not open %s\n", outputPath);
		return 1;
	}
	
	for(int n = 0; n < NUM_OUTPUT_CHANNELS; n++)
	{
		outputChannels[n].Close();
	}
	
	uint16_t outputPosition = 8;
	for(int n = 0; n < 4; n++)
	{
		if(n < NUM_OUTPUT_CHANNELS)
		{
			fwrite(&outputPosition, 2, 1, outputFile);
			outputPosition += outputChannels[n].outputStream.size();
		}
		else
		{
			uint16_t position = outputPosition - 2;
			fwrite(&position, 2, 1, outputFile);
		}
	}
	
	for(int n = 0; n < NUM_OUTPUT_CHANNELS; n++)
	{
		fwrite(outputChannels[n].outputStream.data(), 1, outputChannels[n].outputStream.size(), outputFile);
	}
	
	delete[] soundData;
	
	return 0;
}
