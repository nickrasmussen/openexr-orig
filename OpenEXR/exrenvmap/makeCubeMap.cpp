///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2004, Industrial Light & Magic, a division of Lucas
// Digital Ltd. LLC
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// *       Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// *       Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
// *       Neither the name of Industrial Light & Magic nor the names of
// its contributors may be used to endorse or promote products derived
// from this software without specific prior written permission. 
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////


//-----------------------------------------------------------------------------
//
//	function makeCubeMap() -- makes cube-face environment maps
//
//-----------------------------------------------------------------------------

#include <makeCubeMap.h>

#include <ImfRgbaFile.h>
#include <ImfTiledRgbaFile.h>
#include <ImfStandardAttributes.h>
#include <EnvmapImage.h>
#include <Iex.h>
#include <iostream>
#include <algorithm>


using namespace std;
using namespace Imf;
using namespace Imath;


void
makeCubeMap (const char inFileName[],
	     const char outFileName[],
	     int tileWidth,
	     int tileHeight,
	     LevelMode levelMode,
	     LevelRoundingMode roundingMode,
	     int mapWidth,
	     float padTop,
	     float padBottom,
	     float filterRadius,
	     int numSamples,
	     bool verbose)
{
    if (levelMode == RIPMAP_LEVELS)
	throw Iex::NoImplExc ("Cannot generate ripmap cube-face environments.");

    //
    // Read the input image, and if necessary,
    // pad the image at the top and bottom.
    //

    EnvmapImage image1;
    Header header;
    RgbaChannels channels;

    {
	RgbaInputFile in (inFileName);

	if (verbose)
	    cout << "reading file " << inFileName << endl;

	header = in.header();
	channels = in.channels();

	Envmap type = ENVMAP_LATLONG;

	if (hasEnvmap (in.header()))
	    type = envmap (in.header());

	const Box2i &dw = in.dataWindow();
	int w = dw.max.x - dw.min.x + 1;
	int h = dw.max.y - dw.min.y + 1;

	int pt = 0;
	int pb = 0;

	if (type == ENVMAP_LATLONG)
	{
	    pt = int (padTop * h + 0.5f);
	    pb = int (padBottom * h + 0.5f);
	}

	Box2i paddedDw (V2i (dw.min.x, dw.min.y - pt),
		        V2i (dw.max.x, dw.max.y + pb));
	
	image1.resize (type, paddedDw);
	Array2D<Rgba> &pixels = image1.pixels();

	in.setFrameBuffer (&pixels[-paddedDw.min.y][-paddedDw.min.x], 1, w);
	in.readPixels (dw.min.y, dw.max.y);

	for (int y = 0; y < pt; ++y)
	    for (int x = 0; x < w; ++x)
		pixels[y][x] = pixels[pt][x];

	for (int y = h + pt; y < h + pt + pb; ++y)
	{
	    for (int x = 0; x < w; ++x)
		pixels[y][x] = pixels[h + pt - 1][x];
	}
    }

    //
    // Open the file that will contain the cube-face map,
    // and write the header.
    //

    int mapHeight = mapWidth * 6;

    header.dataWindow() = Box2i (V2i (0, 0), V2i (mapWidth - 1, mapHeight - 1));
    header.displayWindow() = header.dataWindow();

    addEnvmap (header, ENVMAP_CUBE);

    TiledRgbaOutputFile out (outFileName,
			     header,
			     channels,
			     tileWidth, tileHeight,
			     levelMode,
			     roundingMode);
    if (verbose)
	cout << "writing file " << outFileName << endl;

    //
    // Generate the pixels for the various levels of the cube-face map,
    // and store them in the file.  The pixels for the highest-resolution
    // level are generated by resampling the original input image; for
    // each of the other levels, the pixels are generated by resampling
    // the previous level.
    //

    EnvmapImage image2;
    EnvmapImage *iptr1 = &image1;
    EnvmapImage *iptr2 = &image2;
    
    for (int level = 0; level < out.numLevels(); ++level)
    {
	if (verbose)
	    cout << "level " << level << endl;

	Box2i dw = out.dataWindowForLevel (level);
	int sof = CubeMap::sizeOfFace (dw);
	float radius = 1.5f * filterRadius / sof;

	iptr2->resize (ENVMAP_CUBE, dw);
	iptr2->clear ();

	Array2D<Rgba> &pixels = iptr2->pixels();

	for (int f = CUBEFACE_POS_X; f <= CUBEFACE_NEG_Z; ++f)
	{
	    if (verbose)
		cout << "    face " << f << endl;

	    CubeMapFace face = CubeMapFace (f);
	    Box2i dwf = CubeMap::dataWindowForFace (face, dw);

	    for (int y = 0; y < sof; ++y)
	    {
		for (int x = 0; x < sof; ++x)
		{
		    V2f posInFace (x, y);
		    V3f dir = CubeMap::direction (face, dw, posInFace);
		    V2f pos = CubeMap::pixelPosition (face, dw, posInFace);
		    
		    pixels[int (pos.y + 0.5f)][int (pos.x + 0.5f)] =
			iptr1->filteredLookup (dir, radius, numSamples);
		}
	    }
	}

	out.setFrameBuffer (&pixels[0][0], 1, dw.max.x + 1);

	for (int tileY = 0; tileY < out.numYTiles (level); ++tileY)
	    for (int tileX = 0; tileX < out.numXTiles (level); ++tileX)
		out.writeTile (tileX, tileY, level);

	swap (iptr1, iptr2);
    }

    if (verbose)
	cout << "done." << endl;
}
