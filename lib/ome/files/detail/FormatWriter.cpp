/*
 * #%L
 * OME-FILES C++ library for image IO.
 * Copyright © 2006 - 2015 Open Microscopy Environment:
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

#include <cmath>
#include <fstream>

#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>

#include <ome/common/filesystem.h>
#include <ome/common/mstream.h>
#include <ome/common/string.h>

#include <ome/compat/regex.h>

#include <ome/files/FormatTools.h>
#include <ome/files/PixelBuffer.h>
#include <ome/files/PixelProperties.h>
#include <ome/files/VariantPixelBuffer.h>
#include <ome/files/detail/FormatWriter.h>

#include <ome/xml/meta/DummyMetadata.h>
#include <ome/xml/meta/FilterMetadata.h>
#include <ome/xml/meta/OMEXMLMetadata.h>

using boost::filesystem::path;
using ome::xml::meta::DummyMetadata;
using ome::xml::meta::FilterMetadata;
using ome::xml::meta::MetadataException;
using ome::files::CoreMetadata;

namespace ome
{
  namespace files
  {
    namespace detail
    {

      namespace
      {
        // Default thumbnail width and height.
        const dimension_size_type THUMBNAIL_DIMENSION = 128;


        MetadataList<Resolution>
        getAllResolutions(::ome::xml::meta::MetadataRetrieve& retrieve)
        {
          MetadataList<Resolution> rl = getResolutions(retrieve);

          // Add full resolutions.
          for (::ome::xml::meta::MetadataRetrieve::index_type image = 0;
               image < retrieve.getImageCount();
               ++image)
            {
              Resolution r = {{ static_cast<dimension_size_type>(retrieve.getPixelsSizeX(image)), static_cast<dimension_size_type>(retrieve.getPixelsSizeY(image)), static_cast<dimension_size_type>(retrieve.getPixelsSizeZ(image)) }};
              auto& series = rl.at(image);
              series.insert(series.begin(), r);
            }

          return rl;
        }

      }

      FormatWriter::FormatWriter(const WriterProperties& writerProperties):
        writerProperties(writerProperties),
        currentId(boost::none),
        out(),
        series(0),
        resolution(0),
        plane(0),
        compression(boost::none),
        interleaved(boost::none),
        sequential(false),
        framesPerSecond(0),
        tile_size_x(boost::none),
        tile_size_y(boost::none),
        metadataRetrieve(std::make_shared<DummyMetadata>()),
        resolutionLevels()
      {
        assertId(currentId, false);
      }

      FormatWriter::~FormatWriter()
      {
        try
          {
            close();
          }
        catch (...)
          {
          }
      }

      void
      FormatWriter::setId(const boost::filesystem::path& id)
      {
        // Attempt to canonicalize the path.
        path canonicalpath = id;
        try
          {
            canonicalpath = ome::common::canonical(id);
          }
        catch (const std::exception&)
          {
          }

        if (!currentId || canonicalpath != currentId.get())
          {
            if (out)
              out = std::shared_ptr<std::ostream>();

            currentId = canonicalpath;
          }
      }

      void
      FormatWriter::close(bool /* fileOnly */)
      {
        out.reset(); // set to null.
        currentId = boost::none;
        series = 0;
        resolution = 0;
        plane = 0;
        compression = boost::none;
        sequential = false;
        framesPerSecond = 0;
        metadataRetrieve.reset();
        resolutionLevels.clear();
      }

      bool
      FormatWriter::isThisType(const boost::filesystem::path& name,
                               bool                           /* open */) const
      {
        return checkSuffix(name,
                           writerProperties.suffixes,
                           writerProperties.compression_suffixes);
      }

      dimension_size_type
      FormatWriter::getSeriesCount() const
      {
        return metadataRetrieve->getImageCount();
      }

      void
      FormatWriter::setLookupTable(dimension_size_type       /* plane */,
                                   const VariantPixelBuffer& /* buf */)
      {
        assertId(currentId, true);

        throw std::runtime_error("Writer does not implement lookup tables");
      }

      void
      FormatWriter::saveBytes(dimension_size_type plane,
                              VariantPixelBuffer& buf)
      {
        assertId(currentId, true);

        dimension_size_type width = getSizeX();
        dimension_size_type height = getSizeY();
        saveBytes(plane, buf, 0, 0, width, height);
      }

      void
      FormatWriter::setSeries(dimension_size_type series)
      {
        assertId(currentId, true);

        if (series >= getSeriesCount())
          {
            boost::format fmt("Invalid series: %1%");
            fmt % series;
            throw std::logic_error(fmt.str());
          }

        const dimension_size_type currentSeries = getSeries();
        if (currentSeries != series &&
            (series > 0 && currentSeries != series - 1))
          {
            boost::format fmt("Series set out of order: %1% (currently %2%)");
            fmt % series % currentSeries;
            throw std::logic_error(fmt.str());
          }

        this->series = series;
        this->resolution = 0U;
        this->plane = 0U;
      }

      dimension_size_type
      FormatWriter::getSeries() const
      {
        assertId(currentId, true);

        return series;
      }

      void
      FormatWriter::setPlane(dimension_size_type plane)
      {
        assertId(currentId, true);

        if (plane >= getImageCount())
          {
            boost::format fmt("Invalid plane: %1%");
            fmt % plane;
            throw std::logic_error(fmt.str());
          }

        const dimension_size_type currentPlane = getPlane();
        if (currentPlane != plane &&
            (plane > 0 && currentPlane != plane - 1))
          {
            boost::format fmt("Plane set out of order: %1% (currently %2%)");
            fmt % plane % currentPlane;
            throw std::logic_error(fmt.str());
          }

        this->plane = plane;
      }

      dimension_size_type
      FormatWriter::getPlane() const
      {
        assertId(currentId, true);

        return plane;
      }

      void
      FormatWriter::setFramesPerSecond(frame_rate_type rate)
      {
        framesPerSecond = rate;
      }

      FormatWriter::frame_rate_type
      FormatWriter::getFramesPerSecond() const
      {
        return framesPerSecond;
      }

      const std::set<ome::xml::model::enums::PixelType>
      FormatWriter::getPixelTypes() const
      {
        return getPixelTypes("default");
      }

      const std::set<ome::xml::model::enums::PixelType>
      FormatWriter::getPixelTypes(const std::string& codec) const
      {
        std::set<ome::xml::model::enums::PixelType> ret;

        for(WriterProperties::pixel_compression_type_map::const_iterator ci = writerProperties.pixel_compression_types.begin();
            ci != writerProperties.pixel_compression_types.end();
            ++ci)
          {
            if (ci->second.find(codec) != ci->second.end())
              ret.insert(ci->first);
          }

        return ret;
      }

      bool
      FormatWriter::isSupportedType(ome::xml::model::enums::PixelType type) const
      {
        return isSupportedType(type, "default");
      }

      bool
      FormatWriter::isSupportedType(ome::xml::model::enums::PixelType type,
                                    const std::string&                codec) const
      {
        WriterProperties::pixel_compression_type_map::const_iterator ci = writerProperties.pixel_compression_types.find(type);
        return ci != writerProperties.pixel_compression_types.end() &&
          ci->second.find(codec) != ci->second.end();
      }

      void
      FormatWriter::setCompression(const std::string& compression)
      {
        std::set<std::string>::const_iterator i = writerProperties.compression_types.find(compression);
        if (i == writerProperties.compression_types.end())
          {
            boost::format fmt("Invalid compression type: %1%");
            fmt % compression;
            throw std::logic_error(fmt.str());
          }

        this->compression = compression;
      }

      const boost::optional<std::string>&
      FormatWriter::getCompression() const
      {
        return this->compression;
      }

      void
      FormatWriter::setInterleaved(bool interleaved)
      {
        this->interleaved = interleaved;
      }

      const boost::optional<bool>&
      FormatWriter::getInterleaved() const
      {
        return interleaved;
      }

      void
      FormatWriter::changeOutputFile(const boost::filesystem::path& id)
      {
        assertId(currentId, true);

        setId(id);
      }

      void
      FormatWriter::setWriteSequentially(bool sequential)
      {
        this->sequential = sequential;
      }

      bool
      FormatWriter::getWriteSequentially() const
      {
        return sequential;
      }

      void
      FormatWriter::setMetadataRetrieve(std::shared_ptr<::ome::xml::meta::MetadataRetrieve>& retrieve)
      {
        assertId(currentId, false);

        if (!retrieve)
          throw std::logic_error("MetadataStore can not be null");

        metadataRetrieve = retrieve;
        resolutionLevels = getAllResolutions(*retrieve);

        // Strip resolution annotations from the metadata store.
        auto store(std::dynamic_pointer_cast<ome::xml::meta::MetadataStore>(retrieve));
        if (store)
          ome::files::removeResolutions(*store);
      }

      const std::shared_ptr<::ome::xml::meta::MetadataRetrieve>&
      FormatWriter::getMetadataRetrieve() const
      {
        return metadataRetrieve;
      }

      std::shared_ptr<::ome::xml::meta::MetadataRetrieve>&
      FormatWriter::getMetadataRetrieve()
      {
        return metadataRetrieve;
      }

      dimension_size_type
      FormatWriter::getImageCount() const
      {
        return getSizeZ() * getSizeT() * getEffectiveSizeC();
      }

      bool
      FormatWriter::isRGB(dimension_size_type channel) const
      {
        return getRGBChannelCount(channel) > 1U;
      }

      dimension_size_type
      FormatWriter::getSizeX() const
      {
        dimension_size_type sizeX = resolutionLevels.at(getSeries()).at(getResolution())[0];
        if (sizeX == 0U)
          sizeX = 1U;
        return sizeX;
      }

      dimension_size_type
      FormatWriter::getSizeY() const
      {
        dimension_size_type sizeY = resolutionLevels.at(getSeries()).at(getResolution())[1];
        if (sizeY == 0U)
          sizeY = 1U;
        return sizeY;
      }

      dimension_size_type
      FormatWriter::getSizeZ() const
      {
        dimension_size_type sizeZ = resolutionLevels.at(getSeries()).at(getResolution())[2];
        if (sizeZ == 0U)
          sizeZ = 1U;
        return sizeZ;
      }

      dimension_size_type
      FormatWriter::getSizeT() const
      {
        dimension_size_type series = getSeries();
        dimension_size_type sizeT = metadataRetrieve->getPixelsSizeT(series);
        if (sizeT == 0U)
          sizeT = 1U;
        return sizeT;
      }

      dimension_size_type
      FormatWriter::getSizeC() const
      {
        dimension_size_type series = getSeries();
        dimension_size_type sizeC = metadataRetrieve->getPixelsSizeC(series);
        if (sizeC == 0U)
          sizeC = 1U;
        return sizeC;
      }

      ome::xml::model::enums::PixelType
      FormatWriter::getPixelType() const
      {
        dimension_size_type series = getSeries();
        return metadataRetrieve->getPixelsType(series);
      }

      pixel_size_type
      FormatWriter::getBitsPerPixel() const
      {
        dimension_size_type series = getSeries();
        return metadataRetrieve->getPixelsSignificantBits(series);
      }

      dimension_size_type
      FormatWriter::getEffectiveSizeC() const
      {
        dimension_size_type series = getSeries();
        return metadataRetrieve->getChannelCount(series);
      }

      dimension_size_type
      FormatWriter::getRGBChannelCount(dimension_size_type channel) const
      {
        dimension_size_type series = getSeries();

        dimension_size_type samples = 1U;

        try
          {
            samples = metadataRetrieve->getChannelSamplesPerPixel(series, channel);
          }
        catch (const MetadataException&)
          {
            // No SamplesPerPixel; default to 1.
          }

        return samples;
      }

      const std::string&
      FormatWriter::getDimensionOrder() const
      {
        dimension_size_type series = getSeries();
        return metadataRetrieve->getPixelsDimensionOrder(series);
      }

      dimension_size_type
      FormatWriter::getIndex(dimension_size_type z,
                             dimension_size_type c,
                             dimension_size_type t) const
      {
        assertId(currentId, true);
        return ome::files::getIndex(getDimensionOrder(),
                                         getSizeZ(),
                                         getEffectiveSizeC(),
                                         getSizeT(),
                                         getImageCount(),
                                         z, c, t);
      }

      std::array<dimension_size_type, 3>
      FormatWriter::getZCTCoords(dimension_size_type index) const
      {
        assertId(currentId, true);
        return ome::files::getZCTCoords(getDimensionOrder(),
                                             getSizeZ(),
                                             getEffectiveSizeC(),
                                             getSizeT(),
                                             getImageCount(),
                                             index);
      }

      const std::string&
      FormatWriter::getFormat() const
      {
        return writerProperties.name;
      }

      const std::string&
      FormatWriter::getFormatDescription() const
      {
        return writerProperties.description;
      }

      const std::vector<boost::filesystem::path>&
      FormatWriter::getSuffixes() const
      {
        return writerProperties.suffixes;
      }

      const std::vector<boost::filesystem::path>&
      FormatWriter::getCompressionSuffixes() const
      {
        return writerProperties.compression_suffixes;
      }

      const std::set<std::string>&
      FormatWriter::getCompressionTypes() const
      {
        return writerProperties.compression_types;
      }

      const std::set<std::string>&
      FormatWriter::getCompressionTypes(ome::xml::model::enums::PixelType type) const
      {
        static std::set<std::string> empty;

        WriterProperties::pixel_compression_type_map::const_iterator ci = writerProperties.pixel_compression_types.find(type);
        if (ci != writerProperties.pixel_compression_types.end())
          return ci->second;
        else
          return empty;
      }

      bool
      FormatWriter::canDoStacks() const
      {
        return writerProperties.stacks;
      }

      dimension_size_type
      FormatWriter::setTileSizeX(boost::optional<dimension_size_type> size)
      {
        tile_size_x = size;
        return getTileSizeX();
      }

      dimension_size_type
      FormatWriter::getTileSizeX() const
      {
        if (!tile_size_x)
          {
            if (!metadataRetrieve)
              // fallback before setId and setMetadataRetrieve
              throw std::logic_error("MetadataStore can not be null");
            if (currentId)
              // after setId
              return getSizeX();
            else
              // fallback before setId
              return metadataRetrieve->getPixelsSizeX(0);
          }
        return *tile_size_x;
      }

      dimension_size_type
      FormatWriter::setTileSizeY(boost::optional<dimension_size_type> size)
      {
        tile_size_y = size;
        return getTileSizeY();
      }


      dimension_size_type
      FormatWriter::getTileSizeY() const
      {
        if (!tile_size_y)
          {
            if (!metadataRetrieve)
              // fallback before setId and setMetadataRetrieve
              throw std::logic_error("MetadataStore can not be null");
            if (currentId)
              // after setId
              return getSizeX();
            else
              // fallback before setId
              return metadataRetrieve->getPixelsSizeX(0);
          }
        return *tile_size_y;
      }

      dimension_size_type
      FormatWriter::getResolutionCount() const
      {
        assertId(currentId, true);

        return resolutionLevels.at(getSeries()).size();
      }

      void
      FormatWriter::setResolution(dimension_size_type resolution)
      {
        assertId(currentId, true);

        if (resolution >= getResolutionCount())
          {
            boost::format fmt("Invalid resolution: %1%");
            fmt % resolution;
            throw std::logic_error(fmt.str());
          }
        // this->series unchanged.
        this->resolution = resolution;
        this->plane = 0;
      }

      dimension_size_type
      FormatWriter::getResolution() const
      {
        assertId(currentId, true);

        return resolution;
      }

    }
  }
}
