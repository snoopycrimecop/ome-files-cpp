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

#include <boost/range/size.hpp>

#include <ome/compat/memory.h>

#include <ome/files/FormatWriter.h>
#include <ome/files/MetadataTools.h>
#include <ome/files/VariantPixelBuffer.h>
#include <ome/files/PixelProperties.h>
#include <ome/files/detail/FormatWriter.h>

#include <ome/xml/meta/MetadataStore.h>
#include <ome/xml/meta/OMEXMLMetadata.h>

#include <ome/xml/model/primitives/PositiveInteger.h>
#include <ome/xml/model/primitives/Timestamp.h>

#include <ome/test/config.h>

#include <boost/filesystem/operations.hpp>

#include <ome/test/test.h>

#include "pixel.h"

using ome::files::CoreMetadata;
using ome::files::EndianType;
using ome::files::FormatWriter;
using ome::files::VariantPixelBuffer;
using ome::files::detail::WriterProperties;
using ome::files::MetadataMap;
using ome::files::MetadataOptions;
using ome::files::createID;
using ome::files::dimension_size_type;
using ome::xml::meta::MetadataStore;
using ome::xml::meta::MetadataRetrieve;
using ome::xml::meta::OMEXMLMetadata;
using ome::xml::model::enums::PixelType;
using ome::xml::model::primitives::PositiveInteger;
using ome::xml::model::primitives::Timestamp;

namespace std
{
  template<class charT, class traits>
  inline std::basic_ostream<charT,traits>&
  operator<< (std::basic_ostream<charT,traits>& os,
              const std::set<ome::xml::model::enums::PixelType>& set)
  {
    os << '(';
    for (std::set<ome::xml::model::enums::PixelType>::const_iterator i = set.begin();
         i != set.end();
         ++i)
      {
        os << *i;
        if (i++ != set.end())
          os << ", ";
      }
    os << ')';

    return os;
  }

  template<class charT, class traits>
  inline std::basic_ostream<charT,traits>&
  operator<< (std::basic_ostream<charT,traits>& os,
              const std::set<std::string>& set)
  {
    os << '(';
    for (std::set<std::string>::const_iterator i = set.begin();
         i != set.end();
         ++i)
      {
        os << *i;
        if (i++ != set.end())
          os << ", ";
      }
    os << ')';

    return os;
  }
}

typedef ome::xml::model::enums::PixelType PT;
typedef std::array<dimension_size_type, 3> dim;

class FormatWriterTestParameters
{
public:
  PT         type;
  EndianType endian;

  FormatWriterTestParameters(PT         type,
                             EndianType endian):
    type(type),
    endian(endian)
  {}
};

template<class charT, class traits>
inline std::basic_ostream<charT,traits>&
operator<< (std::basic_ostream<charT,traits>& os,
            const FormatWriterTestParameters& params)
{
  return os << PT(params.type) << '/'<< params.endian;
}

namespace
{

  WriterProperties
  test_properties()
  {
    WriterProperties p("TestWriter", "Writer for unit testing");
    p.suffixes.push_back("test");
    p.compression_suffixes.push_back("gz");

    const PixelType::value_map_type& pv = PixelType::values();
    for (PixelType::value_map_type::const_iterator i = pv.begin();
         i != pv.end();
         ++i)
      {
        std::set<std::string> codecset;
        if (i->first != PixelType::INT16 &&
            i->first != PixelType::DOUBLE &&
            i->first != PixelType::COMPLEXDOUBLE &&
            i->first != PixelType::BIT)
          codecset.insert("default");
        codecset.insert("lzw");
        if (i->first == PixelType::BIT)
          codecset.insert("rle");
        if (i->first == PixelType::INT8 || i->first == PixelType::UINT8)
          codecset.insert("test-8bit-only");

        p.compression_types.insert(codecset.begin(), codecset.end());
        p.pixel_compression_types.insert(WriterProperties::pixel_compression_type_map::value_type(i->first, codecset));
      }

    p.stacks = true;

    return p;
  }

  const WriterProperties props(test_properties());

}

class FormatWriterCustom : public ::ome::files::detail::FormatWriter
{
private:
  const FormatWriterTestParameters& test_params;
  VariantPixelBuffer last_plane;

public:
  FormatWriterCustom(const FormatWriterTestParameters& test_params):
    ::ome::files::detail::FormatWriter(props),
    test_params(test_params)
  {
  }

  virtual ~FormatWriterCustom()
  {
  }

  void
  saveBytes(dimension_size_type no,
            VariantPixelBuffer& buf)
  {
    FormatWriter::saveBytes(no, buf);
  }

  void
  saveBytes(dimension_size_type /* no */,
            VariantPixelBuffer& /* buf */,
            dimension_size_type /* x */,
            dimension_size_type /* y */,
            dimension_size_type /* w */,
            dimension_size_type /* h */)
  {
    assertId(currentId, true);
  }

  const VariantPixelBuffer&
  getLastPlane() const
  {
    return last_plane;
  }

private:
  void
  makeMetadata(std::shared_ptr<::ome::xml::meta::MetadataStore> store,
               dimension_size_type                              series,
               std::shared_ptr<CoreMetadata>                    core)
  {
    store->setImageID(createID("Image", series), series);
    store->setImageAcquisitionDate
      (ome::xml::model::primitives::Timestamp("2014-09-11T16:58:43.232"), series);
    store->setImageName("Test Write", series);
    store->setPixelsID(createID("Pixels", series), series);
    store->setPixelsBigEndian(!core->littleEndian, series);
    store->setPixelsDimensionOrder(core->dimensionOrder, series);
    store->setPixelsType(core->pixelType, series);
    store->setPixelsSizeX(PositiveInteger(core->sizeX), series);
    store->setPixelsSizeY(PositiveInteger(core->sizeY), series);
    store->setPixelsSizeZ(PositiveInteger(core->sizeZ), series);
    store->setPixelsSizeT(PositiveInteger(core->sizeT), series);
    store->setPixelsSizeC(PositiveInteger(std::accumulate(core->sizeC.begin(), core->sizeC.end(), dimension_size_type(0))), series);

    dimension_size_type effSizeC = core->sizeC.size();

    for (dimension_size_type c = 0; c < effSizeC; ++c)
      {
        store->setChannelID(createID("Channel", series, c), series, c);
        store->setChannelSamplesPerPixel(PositiveInteger(core->sizeC.at(c)), series, c);
      }
  }

  std::shared_ptr<CoreMetadata>
  makeCore()
  {
    std::shared_ptr<CoreMetadata> c(std::make_shared<CoreMetadata>());

    c->sizeX = 512;
    c->sizeY = 1024;
    c->sizeZ = 20;
    c->sizeT = 4;

    c->sizeC.clear();
    c->sizeC.push_back(1);
    c->sizeC.push_back(1);

    c->pixelType = test_params.type;
    c->imageCount = c->sizeZ * c->sizeT * std::accumulate(c->sizeC.begin(), c->sizeC.end(), dimension_size_type(0));
    c->orderCertain = true;
    c->littleEndian = test_params.endian == ::ome::files::ENDIAN_LITTLE;
    c->interleaved = false;
    c->indexed = false;
    c->falseColor = true;
    c->metadataComplete = false;
    c->thumbnail = false;
    c->resolutionCount = 1;

    return c;
  }

public:
  void
  setId(const boost::filesystem::path& id)
  {
    if (!currentId)
      {
        std::shared_ptr<::ome::xml::meta::OMEXMLMetadata> m(std::make_shared<::ome::xml::meta::OMEXMLMetadata>());

        if (id == "output.test")
          {
            // 4 series
            makeMetadata(m, 0, makeCore());
            makeMetadata(m, 1, makeCore());
            makeMetadata(m, 2, makeCore());
            makeMetadata(m, 3, makeCore());
          }

        std::shared_ptr<::ome::xml::meta::MetadataRetrieve> mr(std::static_pointer_cast<::ome::xml::meta::MetadataRetrieve>(m));
        setMetadataRetrieve(mr);

        FormatWriter::setId(id);
      }
  }
};

class FormatWriterTest : public ::testing::TestWithParam<FormatWriterTestParameters>
{
public:
  FormatWriterCustom w;
  const FormatWriter& cw;

  FormatWriterTest():
    ::testing::TestWithParam<FormatWriterTestParameters>(),
    w(GetParam()),
    cw(w)
  {
  }

  void SetUp()
  {
  }
};

TEST_P(FormatWriterTest, Construct)
{
  FormatWriterCustom w(GetParam());
}

TEST_P(FormatWriterTest, WriterProperties)
{
  w.setId("output.test");
  ASSERT_EQ(props.name, w.getFormat());
  ASSERT_EQ(props.description, w.getFormatDescription());
  ASSERT_TRUE(props.suffixes == w.getSuffixes());
  ASSERT_TRUE(props.compression_suffixes == w.getCompressionSuffixes());
  ASSERT_EQ(props.compression_types, w.getCompressionTypes());
  ASSERT_EQ(props.stacks, w.canDoStacks());
}

TEST_P(FormatWriterTest, IsThisType)
{
  EXPECT_FALSE(w.isThisType("invalid.file"));
  EXPECT_FALSE(w.isThisType("invalid.file", true));
  EXPECT_FALSE(w.isThisType("invalid.file", false));

  EXPECT_TRUE(w.isThisType("valid.test"));
  EXPECT_TRUE(w.isThisType("valid.test", true));
  EXPECT_TRUE(w.isThisType("valid.test", false));
}

TEST_P(FormatWriterTest, DefaultLUT)
{
  VariantPixelBuffer buf(boost::extents[256][1][1][1][1][3][1][1][1]);
  EXPECT_THROW(w.setLookupTable(0U, buf), std::logic_error);
}

TEST_P(FormatWriterTest, OutputLUT)
{
  w.setId("output.test");

  VariantPixelBuffer buf(boost::extents[256][1][1][1][1][3][1][1][1]);
  EXPECT_THROW(w.setLookupTable(0U, buf), std::runtime_error);
}

TEST_P(FormatWriterTest, DefaultPixels)
{
  const FormatWriterTestParameters& params(GetParam());

  VariantPixelBuffer buf(boost::extents[512][512][1][1][2][1][1][1][1],
                         params.type);

  EXPECT_THROW(w.saveBytes(0, buf), std::logic_error);
  EXPECT_THROW(w.saveBytes(0, buf, 0, 0, 512, 512), std::logic_error);
}

namespace
{

  struct OutputPixelsTest
  {
    const FormatWriterTestParameters& params;
    FormatWriterCustom& writer;
    VariantPixelBuffer& buf;

    OutputPixelsTest(const FormatWriterTestParameters& params,
                     FormatWriterCustom& writer,
                     VariantPixelBuffer& buf):
      params(params),
      writer(writer),
      buf(buf)
    {}

    template<typename T>
    void
    operator()(T& /* v */) const
    {
      typedef typename T::element_type::value_type value_type;

      std::stringstream ss;
      std::vector<value_type> expected;
      for (uint32_t x = 0; x < 512; ++x)
        for (uint32_t y = 0; y < 512; ++y)
          {
            value_type val(pixel_value<value_type>(x*y));

            expected.push_back(val);

            if ((params.endian == ome::files::ENDIAN_BIG &&
                 boost::endian::order::big != boost::endian::order::native) ||
                (params.endian == ome::files::ENDIAN_LITTLE &&
                 boost::endian::order::little != boost::endian::order::native))
              ome::files::byteswap(val);

            ss.write(reinterpret_cast<char *>(&val), sizeof(val));
          }

      VariantPixelBuffer buf(boost::extents[512][512][1][1][2][1][1][1][1],
                             params.type);

      // ss.seekg(0, std::ios::beg);
      // EXPECT_NO_THROW(writer.readPlane(ss, buf, 0, 0, 512, 512));
      // ss.seekg(0, std::ios::beg);
      // EXPECT_NO_THROW(writer.readPlane(ss, buf, 0, 0, 512, 512, 0));

      // ASSERT_EQ(expected.size(), buf.num_elements());
      // for (uint32_t i = 0; i < expected.size(); ++i)
      //   ASSERT_EQ(expected.at(i), *(buf.data<value_type>()+i));

      EXPECT_NO_THROW(writer.saveBytes(0, buf));
      EXPECT_NO_THROW(writer.saveBytes(0, buf, 0, 0, 512, 512));
    }
  };

}

TEST_P(FormatWriterTest, OutputPixels)
{
  const FormatWriterTestParameters& params(GetParam());

  w.setId("output.test");

  VariantPixelBuffer buf(boost::extents[512][512][1][1][2][1][1][1][1],
                         params.type);

  OutputPixelsTest v(params, w, buf);
  ome::compat::visit(v, buf.vbuffer());
}

TEST_P(FormatWriterTest, DefaultSeries)
{
  EXPECT_THROW(w.setSeries(0U), std::logic_error);
  EXPECT_THROW(w.setSeries(2U), std::logic_error);
  EXPECT_THROW(w.setSeries(4U), std::logic_error);
}

TEST_P(FormatWriterTest, OutputSeries)
{
  w.setId("output.test");

  // Current series is OK.
  EXPECT_NO_THROW(w.setSeries(0U));
  // Series is valid but skips series 1.
  EXPECT_THROW(w.setSeries(2U), std::logic_error);
  // Series is invalid
  EXPECT_THROW(w.setSeries(4U), std::logic_error);
}

TEST_P(FormatWriterTest, DefaultFrameRate)
{
  EXPECT_EQ(0U, w.getFramesPerSecond());
  EXPECT_NO_THROW(w.setFramesPerSecond(5U));
  EXPECT_EQ(5U, w.getFramesPerSecond());
}

TEST_P(FormatWriterTest, OutputFrameRate)
{
  w.setId("output.test");

  EXPECT_EQ(0U, w.getFramesPerSecond());
  EXPECT_NO_THROW(w.setFramesPerSecond(5U));
  EXPECT_EQ(5U, w.getFramesPerSecond());
}

TEST_P(FormatWriterTest, DefaultCompression)
{
  EXPECT_FALSE(w.getCompression());
  EXPECT_NO_THROW(w.setCompression("lzw"));
  EXPECT_TRUE(!!w.getCompression());
  EXPECT_EQ(std::string("lzw"), w.getCompression().get());
  EXPECT_NO_THROW(w.setCompression("rle"));
  EXPECT_TRUE(!!w.getCompression());
  EXPECT_EQ(std::string("rle"), w.getCompression().get());
  EXPECT_THROW(w.setCompression("invalid"), std::logic_error);
  EXPECT_TRUE(!!w.getCompression());
  EXPECT_EQ(std::string("rle"), w.getCompression().get());
}

TEST_P(FormatWriterTest, OutputCompression)
{
  w.setId("output.test");

  EXPECT_FALSE(w.getCompression());
  EXPECT_NO_THROW(w.setCompression("lzw"));
  EXPECT_TRUE(!!w.getCompression());
  EXPECT_EQ(std::string("lzw"), w.getCompression().get());
  EXPECT_NO_THROW(w.setCompression("rle"));
  EXPECT_TRUE(!!w.getCompression());
  EXPECT_EQ(std::string("rle"), w.getCompression().get());
  EXPECT_THROW(w.setCompression("invalid"), std::logic_error);
  EXPECT_TRUE(!!w.getCompression());
  EXPECT_EQ(std::string("rle"), w.getCompression().get());
}

TEST_P(FormatWriterTest, DefaultChangeOutputFile)
{
  ASSERT_THROW(w.changeOutputFile("output2.test"), std::logic_error);
}

TEST_P(FormatWriterTest, OutputChangeOutputFile)
{
  w.setId("output.test");

  EXPECT_NO_THROW(w.changeOutputFile("output2.test"));
  EXPECT_NO_THROW(w.changeOutputFile("output3.test"));
  EXPECT_NO_THROW(w.changeOutputFile("output4.test"));
  EXPECT_NO_THROW(w.changeOutputFile("output5.test"));
}

TEST_P(FormatWriterTest, DefaultSequential)
{
  EXPECT_FALSE(w.getWriteSequentially());
  EXPECT_NO_THROW(w.setWriteSequentially(true));
  EXPECT_TRUE(w.getWriteSequentially());
  EXPECT_NO_THROW(w.setWriteSequentially(false));
  EXPECT_FALSE(w.getWriteSequentially());
  EXPECT_NO_THROW(w.setWriteSequentially());
  EXPECT_TRUE(w.getWriteSequentially());
}

TEST_P(FormatWriterTest, OutputSequential)
{
  w.setId("output.test");

  EXPECT_FALSE(w.getWriteSequentially());
  EXPECT_NO_THROW(w.setWriteSequentially(true));
  EXPECT_TRUE(w.getWriteSequentially());
  EXPECT_NO_THROW(w.setWriteSequentially(false));
  EXPECT_FALSE(w.getWriteSequentially());
  EXPECT_NO_THROW(w.setWriteSequentially());
  EXPECT_TRUE(w.getWriteSequentially());
}

TEST_P(FormatWriterTest, CompressionTypes)
{
  const std::set<std::string>& ctypes = w.getCompressionTypes();

  std::cout << "Supported compression types:\n";
  for (std::set<std::string>::const_iterator i = ctypes.begin();
       i != ctypes.end();
       ++i)
    {
      std::cout << "  " << *i << '\n';
    }

  // Dump per-pixel type codec list
  const PixelType::value_map_type& pv = PixelType::values();
  for (PixelType::value_map_type::const_iterator i = pv.begin();
       i != pv.end();
       ++i)
    {
      std::cout << "Pixel Type: " << i->second << '\n';
      const std::set<std::string>& types = w.getCompressionTypes(i->first);
      for (std::set<std::string>::const_iterator t = types.begin();
           t != types.end();
           ++t)
        std::cout << "  " << *t << '\n';
    }
}

TEST_P(FormatWriterTest, PixelTypesDefault)
{
  std::set<ome::xml::model::enums::PixelType> default_pixel_types;
  const PixelType::value_map_type& pv = PixelType::values();
  for (PixelType::value_map_type::const_iterator i = pv.begin();
       i != pv.end();
       ++i)
    {
      if (i->first != PixelType::INT16 &&
          i->first != PixelType::DOUBLE &&
          i->first != PixelType::COMPLEXDOUBLE &&
          i->first != PixelType::BIT)
      default_pixel_types.insert(i->first);
    }

  const std::set<ome::xml::model::enums::PixelType>& pt = w.getPixelTypes();
  EXPECT_EQ(default_pixel_types, pt);
}

TEST_P(FormatWriterTest, PixelTypesByCodec)
{
  const PixelType::value_map_type& pv = PixelType::values();

  std::set<ome::xml::model::enums::PixelType> all_pixel_types;
  for (PixelType::value_map_type::const_iterator i = pv.begin();
       i != pv.end();
       ++i)
    all_pixel_types.insert(i->first);

  std::set<ome::xml::model::enums::PixelType> default_pixel_types;
  for (PixelType::value_map_type::const_iterator i = pv.begin();
       i != pv.end();
       ++i)
    {
      if (i->first != PixelType::INT16 &&
          i->first != PixelType::DOUBLE &&
          i->first != PixelType::COMPLEXDOUBLE &&
          i->first != PixelType::BIT)
      default_pixel_types.insert(i->first);
    }

  const std::set<ome::xml::model::enums::PixelType> dpt = w.getPixelTypes("default");
  EXPECT_EQ(default_pixel_types, dpt);

  const std::set<ome::xml::model::enums::PixelType> lzw = w.getPixelTypes("lzw");
  EXPECT_EQ(all_pixel_types, lzw);

  std::set<ome::xml::model::enums::PixelType> rle_pixel_types;
  rle_pixel_types.insert(PixelType::BIT);
  const std::set<ome::xml::model::enums::PixelType> rle = w.getPixelTypes("rle");
  EXPECT_EQ(rle_pixel_types, rle);

  std::set<ome::xml::model::enums::PixelType> test_8bit_pixel_types;
  test_8bit_pixel_types.insert(PixelType::INT8);
  test_8bit_pixel_types.insert(PixelType::UINT8);
  const std::set<ome::xml::model::enums::PixelType> test_8bit = w.getPixelTypes("test-8bit-only");
  EXPECT_EQ(test_8bit_pixel_types, test_8bit);

  const std::set<ome::xml::model::enums::PixelType> ipt = w.getPixelTypes("invalid");
  EXPECT_TRUE(ipt.empty());
}

TEST_P(FormatWriterTest, SupportedPixelTypeDefault)
{
  EXPECT_TRUE(w.isSupportedType(ome::xml::model::enums::PixelType::UINT8));
  EXPECT_TRUE(w.isSupportedType(ome::xml::model::enums::PixelType::UINT16));
  EXPECT_TRUE(w.isSupportedType(ome::xml::model::enums::PixelType::UINT32));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::DOUBLE));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::COMPLEXDOUBLE));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::BIT));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::INT16));
}

TEST_P(FormatWriterTest, SupportedPixelTypeByCodec)
{
  EXPECT_TRUE(w.isSupportedType(ome::xml::model::enums::PixelType::UINT8, "default"));
  EXPECT_TRUE(w.isSupportedType(ome::xml::model::enums::PixelType::UINT16, "default"));
  EXPECT_TRUE(w.isSupportedType(ome::xml::model::enums::PixelType::UINT32, "default"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::DOUBLE, "default"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::COMPLEXDOUBLE, "default"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::BIT, "default"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::INT16, "default"));

  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::UINT8, "rle"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::UINT16, "rle"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::UINT32, "rle"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::DOUBLE, "rle"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::COMPLEXDOUBLE, "rle"));
  EXPECT_TRUE(w.isSupportedType(ome::xml::model::enums::PixelType::BIT, "rle"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::INT16, "rle"));

  EXPECT_TRUE(w.isSupportedType(ome::xml::model::enums::PixelType::UINT8, "test-8bit-only"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::UINT16, "test-8bit-only"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::UINT32, "test-8bit-only"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::DOUBLE, "test-8bit-only"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::COMPLEXDOUBLE, "test-8bit-only"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::BIT, "test-8bit-only"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::INT16, "test-8bit-only"));

  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::UINT8, "invalid"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::UINT16, "invalid"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::UINT32, "invalid"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::DOUBLE, "invalid"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::COMPLEXDOUBLE, "invalid"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::BIT, "invalid"));
  EXPECT_FALSE(w.isSupportedType(ome::xml::model::enums::PixelType::INT16, "invalid"));
}

TEST_P(FormatWriterTest, DefaultMetadataRetrieve)
{
  std::shared_ptr<::ome::xml::meta::OMEXMLMetadata> m(std::make_shared<::ome::xml::meta::OMEXMLMetadata>());
  std::shared_ptr<::ome::xml::meta::MetadataRetrieve> mr(std::static_pointer_cast<::ome::xml::meta::MetadataRetrieve>(m));

  EXPECT_NO_THROW(w.getMetadataRetrieve());
  EXPECT_NO_THROW(w.setMetadataRetrieve(mr));
  EXPECT_NO_THROW(w.getMetadataRetrieve());

  EXPECT_NO_THROW(cw.getMetadataRetrieve());
}

TEST_P(FormatWriterTest, OutputMetadataRetrieve)
{
  std::shared_ptr<const ::ome::xml::meta::MetadataRetrieve> mr;
  std::shared_ptr<::ome::xml::meta::OMEXMLMetadata> m2(std::make_shared<::ome::xml::meta::OMEXMLMetadata>());
  std::shared_ptr<::ome::xml::meta::MetadataRetrieve> mr2(std::static_pointer_cast<::ome::xml::meta::MetadataRetrieve>(m2));

  w.setId("output.test");

  EXPECT_NO_THROW(mr = w.getMetadataRetrieve());
  ASSERT_EQ(4U, mr->getImageCount());
  EXPECT_THROW(w.setMetadataRetrieve(mr2), std::logic_error);

  EXPECT_NO_THROW(cw.getMetadataRetrieve());
}

std::vector<FormatWriterTestParameters> variant_params
  { // PixelType        EndianType
    {PT::INT8,          ome::files::ENDIAN_BIG},
    {PT::INT8,          ome::files::ENDIAN_LITTLE},

    {PT::INT16,         ome::files::ENDIAN_BIG},
    {PT::INT16,         ome::files::ENDIAN_LITTLE},

    {PT::INT32,         ome::files::ENDIAN_BIG},
    {PT::INT32,         ome::files::ENDIAN_LITTLE},

    {PT::UINT8,         ome::files::ENDIAN_BIG},
    {PT::UINT8,         ome::files::ENDIAN_LITTLE},

    {PT::UINT16,        ome::files::ENDIAN_BIG},
    {PT::UINT16,        ome::files::ENDIAN_LITTLE},

    {PT::UINT32,        ome::files::ENDIAN_BIG},
    {PT::UINT32,        ome::files::ENDIAN_LITTLE},

    {PT::FLOAT,         ome::files::ENDIAN_BIG},
    {PT::FLOAT,         ome::files::ENDIAN_LITTLE},

    {PT::DOUBLE,        ome::files::ENDIAN_BIG},
    {PT::DOUBLE,        ome::files::ENDIAN_LITTLE},

    {PT::BIT,           ome::files::ENDIAN_BIG},
    {PT::BIT,           ome::files::ENDIAN_LITTLE},

    {PT::COMPLEXFLOAT,  ome::files::ENDIAN_BIG},
    {PT::COMPLEXFLOAT,  ome::files::ENDIAN_LITTLE},

    {PT::COMPLEXDOUBLE, ome::files::ENDIAN_BIG},
    {PT::COMPLEXDOUBLE, ome::files::ENDIAN_LITTLE}
  };

// Disable missing-prototypes warning for INSTANTIATE_TEST_CASE_P;
// this is solely to work around a missing prototype in gtest.
#ifdef __GNUC__
#  if defined __clang__ || defined __APPLE__
#    pragma GCC diagnostic ignored "-Wmissing-prototypes"
#  endif
#  pragma GCC diagnostic ignored "-Wmissing-declarations"
#endif

INSTANTIATE_TEST_CASE_P(FormatWriterVariants, FormatWriterTest, ::testing::ValuesIn(variant_params));
