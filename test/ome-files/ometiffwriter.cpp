/*
 * #%L
 * OME-FILES C++ library for image IO.
 * %%
 * Copyright © 2013 - 2015 Open Microscopy Environment:
 *   - Massachusetts Institute of Technology
 *   - National Institutes of Health
 *   - University of Dundee
 *   - Board of Regents of the University of Wisconsin-Madison
 *   - Glencoe Software, Inc.
 * Copyright © 2018 Quantitative Imaging Systems, LLC
 * %%
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of any organization.
 * #L%
 */

#include <stdexcept>
#include <vector>

#include <ome/files/CoreMetadata.h>
#include <ome/files/MetadataTools.h>
#include <ome/files/VariantPixelBuffer.h>
#include <ome/files/in/OMETIFFReader.h>
#include <ome/files/out/OMETIFFWriter.h>
#include <ome/files/tiff/Field.h>
#include <ome/files/tiff/IFD.h>
#include <ome/files/tiff/Tags.h>
#include <ome/files/tiff/TIFF.h>
#include <ome/files/tiff/Util.h>

#include <ome/xml/meta/OMEXMLMetadata.h>

#include <ome/test/test.h>

#include "tiffsamples.h"

using ome::files::dimension_size_type;
using ome::files::CoreMetadata;
using ome::files::PixelBufferBase;
using ome::files::VariantPixelBuffer;
using ome::files::in::OMETIFFReader;
using ome::files::out::OMETIFFWriter;
using ome::files::tiff::IFD;
using ome::files::tiff::TIFF;

using namespace boost::filesystem;

class OMETIFFWriterTest : public ::testing::TestWithParam<TIFFTestParameters>
{
public:
  std::shared_ptr<TIFF> tiff;
  uint32_t iwidth;
  uint32_t iheight;
  ome::files::tiff::PlanarConfiguration planarconfig;
  uint16_t samples;

  OMETIFFWriter tiffwriter;
  path testfile;

  void
  SetUp()
  {
    const TIFFTestParameters& params = GetParam();

    path dir(PROJECT_BINARY_DIR "/test/ome-files/data");
    testfile = dir / (std::string("ometiffwriter-") + path(params.file).filename().string());
    testfile.replace_extension(".ome.tiff");

    ASSERT_NO_THROW(tiff = TIFF::open(params.file, "r"));
    ASSERT_TRUE(static_cast<bool>(tiff));
    std::shared_ptr<IFD> ifd;
    ASSERT_NO_THROW(ifd = tiff->getDirectoryByIndex(0));
    ASSERT_TRUE(static_cast<bool>(ifd));

    ASSERT_NO_THROW(ifd->getField(ome::files::tiff::IMAGEWIDTH).get(iwidth));
    ASSERT_NO_THROW(ifd->getField(ome::files::tiff::IMAGELENGTH).get(iheight));
    ASSERT_NO_THROW(ifd->getField(ome::files::tiff::PLANARCONFIG).get(planarconfig));
    ASSERT_NO_THROW(ifd->getField(ome::files::tiff::SAMPLESPERPIXEL).get(samples));
  }

  void
  TearDown()
  {
    // Delete file (if any)
    // if (boost::filesystem::exists(testfile))
    //   boost::filesystem::remove(testfile);
  }
};

TEST_P(OMETIFFWriterTest, setId)
{
  const TIFFTestParameters& params = GetParam();

  std::vector<std::shared_ptr<CoreMetadata>> seriesList;
  for (const auto& i : *tiff)
    {
      std::shared_ptr<CoreMetadata> c = ome::files::tiff::makeCoreMetadata(*i);
      seriesList.push_back(c);
    }

  std::shared_ptr<::ome::xml::meta::OMEXMLMetadata> meta(std::make_shared<::ome::xml::meta::OMEXMLMetadata>());
  ome::files::fillMetadata(*meta, seriesList);
  std::shared_ptr<::ome::xml::meta::MetadataRetrieve> retrieve(std::static_pointer_cast<::ome::xml::meta::MetadataRetrieve>(meta));

  tiffwriter.setMetadataRetrieve(retrieve);

  tiffwriter.setInterleaved(!params.imageplanar);
  tiffwriter.setCompression("Deflate");
  tiffwriter.setTileSizeX(params.tilewidth);
  tiffwriter.setTileSizeY(params.tilelength);

  ASSERT_NO_THROW(tiffwriter.setId(testfile));

  VariantPixelBuffer buf;
  dimension_size_type currentSeries = 0U;
  for (dimension_size_type i = 0U; i < seriesList.size(); ++i)
    {
      std::shared_ptr<IFD> ifd = tiff->getDirectoryByIndex(i);
      ASSERT_TRUE(static_cast<bool>(ifd));
      ifd->readImage(buf);

      // Make a second buffer to ensure correct ordering for saveBytes.
      std::array<VariantPixelBuffer::size_type, PixelBufferBase::dimensions>
        shape{ifd->getImageWidth(), ifd->getImageHeight(), 1U, ifd->getSamplesPerPixel()};

      ome::files::PixelBufferBase::storage_order_type order(ome::files::PixelBufferBase::make_storage_order(!params.imageplanar));

      VariantPixelBuffer src(shape, ifd->getPixelType(), order);
      src = buf;

      ASSERT_NO_THROW(tiffwriter.setSeries(currentSeries));
      ASSERT_NO_THROW(tiffwriter.saveBytes(0, src));
      ++currentSeries;
    }
  tiffwriter.close();

  // Read and validate OME-TIFF
  {
    OMETIFFReader tiffreader;
    std::shared_ptr<ome::xml::meta::MetadataStore> store(std::make_shared<ome::xml::meta::OMEXMLMetadata>());
    ASSERT_NO_THROW(tiffreader.setMetadataStore(store));

    ASSERT_NO_THROW(tiffreader.setId(testfile));

    ASSERT_EQ(seriesList.size(), tiffreader.getSeriesCount());
    for(dimension_size_type i = 0; i < tiffreader.getSeriesCount(); ++i)
      {
        tiffreader.setSeries(i);
        const std::shared_ptr<CoreMetadata> ref = seriesList.at(i);

        EXPECT_EQ(ref->sizeX, tiffreader.getSizeX());
        EXPECT_EQ(ref->sizeY, tiffreader.getSizeY());
        EXPECT_EQ(ref->sizeZ, tiffreader.getSizeZ());
        EXPECT_EQ(ref->sizeT, tiffreader.getSizeT());
        EXPECT_EQ(ref->sizeC.size(), tiffreader.getEffectiveSizeC());
        if (params.tilewidth)
          {
            EXPECT_EQ(*params.tilewidth, tiffreader.getOptimalTileWidth(0));
          }
        if (params.tilelength)
          {
            EXPECT_EQ(*params.tilelength, tiffreader.getOptimalTileHeight(0));
          }
        EXPECT_EQ(ome::xml::model::enums::PixelType::UINT8, tiffreader.getPixelType());
        EXPECT_EQ(8, tiffreader.getBitsPerPixel());
        EXPECT_EQ(3, tiffreader.getRGBChannelCount(0));
        EXPECT_EQ(!params.imageplanar, tiffreader.isInterleaved());

        VariantPixelBuffer buf;
        std::shared_ptr<IFD> ifd = tiff->getDirectoryByIndex(i);
        ASSERT_TRUE(static_cast<bool>(ifd));
        ifd->readImage(buf);

        VariantPixelBuffer vb;
        tiffreader.openBytes(0, vb);

        EXPECT_TRUE(buf == vb);
      }
  }

}

std::vector<TIFFTestParameters> params(find_tiff_tests());

// Disable missing-prototypes warning for INSTANTIATE_TEST_CASE_P;
// this is solely to work around a missing prototype in gtest.
#ifdef __GNUC__
#  if defined __clang__ || defined __APPLE__
#    pragma GCC diagnostic ignored "-Wmissing-prototypes"
#  endif
#  pragma GCC diagnostic ignored "-Wmissing-declarations"
#endif

INSTANTIATE_TEST_CASE_P(OMETIFFWriterVariants, OMETIFFWriterTest, ::testing::ValuesIn(params));
