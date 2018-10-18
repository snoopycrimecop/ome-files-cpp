/*
 * #%L
 * OME-FILES C++ library for image IO.
 * Copyright © 2006 - 2014 Open Microscopy Environment:
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

#include <cassert>

// Include first due to side effect of MPL vector limit setting which can change the default
// and break multi_index with Boost 1.67
#include <ome/xml/meta/Convert.h>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/format.hpp>
#include <boost/range/size.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>

#include <ome/files/FormatException.h>
#include <ome/files/FormatTools.h>
#include <ome/files/MetadataTools.h>
#include <ome/files/out/OMETIFFWriter.h>
#include <ome/files/tiff/Codec.h>
#include <ome/files/tiff/Field.h>
#include <ome/files/tiff/IFD.h>
#include <ome/files/tiff/Tags.h>
#include <ome/files/tiff/TIFF.h>
#include <ome/files/tiff/Util.h>

#include <ome/common/endian.h>
#include <ome/common/filesystem.h>

#include <tiffio.h>

using boost::filesystem::path;

using ome::files::getOMEXML;
using ome::files::detail::WriterProperties;
using ome::files::tiff::TIFF;
using ome::files::tiff::IFD;
using ome::files::tiff::enableBigTIFF;

using ome::common::make_relative;

using ome::xml::model::enums::DimensionOrder;
using ome::xml::model::enums::PixelType;
using ome::xml::meta::convert;
using ome::xml::meta::MetadataRetrieve;
using ome::xml::meta::OMEXMLMetadata;

namespace ome
{
  namespace files
  {
    namespace out
    {

      namespace
      {

        WriterProperties
        tiff_properties()
        {
          WriterProperties p("OME-TIFF",
                             "Open Microscopy Environment TIFF");

          // Note that tf2, tf8 and btf are all extensions for
          // "bigTIFF" (2nd generation TIFF, TIFF with 8-byte offsets
          // and big TIFF respectively).
          p.suffixes = {"ome.tif", "ome.tiff", "ome.tf2", "ome.tf8", "ome.btf"};

          for (const auto& pixeltype : PixelType::values())
            {
              const std::vector<std::string>& ptcodecs = tiff::getCodecNames(pixeltype.first);
              std::set<std::string> codecset(ptcodecs.begin(), ptcodecs.end());
              // Supported by default with no compression
              codecset.insert("default");
              p.compression_types.insert(codecset.begin(), codecset.end());
              p.pixel_compression_types.insert(WriterProperties::pixel_compression_type_map::value_type(pixeltype.first, codecset));
            }

          return p;
        }

        const WriterProperties props(tiff_properties());

        const std::vector<path> companion_suffixes{"companion.ome"};

        const std::string default_description("OME-TIFF");

        /**
         * @todo Move these stream helpers to a proper location,
         * i.e. to replicate the equivalent Java helpers.
         */

        // No switch default to avoid -Wunreachable-code errors.
        // However, this then makes -Wswitch-default complain.  Disable
        // temporarily.
#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wswitch-default"
#endif

        template<typename T,
                 typename B,
                 typename L,
                 typename N>
        T
        read_raw(std::istream&  in,
                 EndianType     endian)
        {
          T ret;

          switch(endian)
            {
            case ENDIAN_BIG:
              {
                B big_val;
                in.read(reinterpret_cast<char *>(&big_val), sizeof(big_val));
                ret = big_val;
                break;
              }
            case ENDIAN_LITTLE:
              {
                L little_val;
                in.read(reinterpret_cast<char *>(&little_val), sizeof(little_val));
                ret = little_val;
                break;
              }
            case ENDIAN_NATIVE:
              {
                N native_val;
                in.read(reinterpret_cast<char *>(&native_val), sizeof(native_val));
                ret = native_val;
                break;
              }
            }

          if (!in)
            throw std::runtime_error("Failed to read value from stream");

          return ret;
        }

#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif

        template<typename T,
                 typename B,
                 typename L,
                 typename N>
        T
        read_raw(std::istream&  in,
                 std::streamoff off,
                 EndianType     endian)
        {
          if (in)
            {
              in.seekg(off, std::ios::beg);
              if (in)
                return read_raw<T, B, L, N>(in, endian);
              else
                throw std::runtime_error("Bad istream offset");
            }
          else
            throw std::runtime_error("Bad istream");
        }

        uint16_t
        read_raw_uint16(std::istream&  in,
                        EndianType     endian)
        {
          return read_raw<uint16_t,
                          boost::endian::big_uint16_t,
                          boost::endian::little_uint16_t,
                          boost::endian::native_uint16_t>(in, endian);
        }

        uint16_t
        read_raw_uint16(std::istream&  in,
                        std::streamoff off,
                        EndianType     endian)
        {
          return read_raw<uint16_t,
                          boost::endian::big_uint16_t,
                          boost::endian::little_uint16_t,
                          boost::endian::native_uint16_t>(in, off, endian);
        }

        uint32_t
        read_raw_uint32(std::istream&  in,
                        std::streamoff off,
                        EndianType     endian)
        {
          return read_raw<uint32_t,
                          boost::endian::big_uint32_t,
                          boost::endian::little_uint32_t,
                          boost::endian::native_uint32_t>(in, off, endian);
        }

        uint64_t
        read_raw_uint64(std::istream&  in,
                        std::streamoff off,
                        EndianType     endian)
        {
          return read_raw<uint64_t,
                          boost::endian::big_uint64_t,
                          boost::endian::little_uint64_t,
                          boost::endian::native_uint64_t>(in, off, endian);
        }

        // No switch default to avoid -Wunreachable-code errors.
        // However, this then makes -Wswitch-default complain.  Disable
        // temporarily.
#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wswitch-default"
#endif

        template<typename T,
                 typename B,
                 typename L,
                 typename N>
        void
        write_raw(std::ostream& in,
                  EndianType    endian,
                  const T&      value)
        {
          switch(endian)
            {
            case ENDIAN_BIG:
              {
                B big_val(value);
                in.write(reinterpret_cast<char *>(&big_val), sizeof(big_val));
                break;
              }
            case ENDIAN_LITTLE:
              {
                L little_val(value);
                in.write(reinterpret_cast<char *>(&little_val), sizeof(little_val));
                break;
              }
            case ENDIAN_NATIVE:
              {
                N native_val(value);
                in.write(reinterpret_cast<char *>(&native_val), sizeof(native_val));
                break;
              }
            }

          if (!in)
            throw std::runtime_error("Failed to write value to stream");
        }

#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif

        template<typename T,
                 typename B,
                 typename L,
                 typename N>
        void
        write_raw(std::ostream&  in,
                  std::streamoff off,
                  EndianType     endian,
                  const T&       value)
        {
          if (in)
            {
              in.seekp(off, std::ios::beg);
              if (in)
                write_raw<T, B, L, N>(in, endian, value);
              else
                throw std::runtime_error("Bad ostream offset");
            }
          else
            throw std::runtime_error("Bad ostream");
        }

        void
        write_raw_uint32(std::ostream&  in,
                         std::streamoff off,
                         EndianType     endian,
                         uint32_t       value)
        {
          write_raw<uint32_t,
                    boost::endian::big_uint32_t,
                    boost::endian::little_uint32_t,
                    boost::endian::native_uint32_t>(in, off, endian, value);
        }

        void
        write_raw_uint64(std::ostream&  in,
                         std::streamoff off,
                         EndianType     endian,
                         uint64_t       value)
        {
          write_raw<uint64_t,
                    boost::endian::big_uint64_t,
                    boost::endian::little_uint64_t,
                    boost::endian::native_uint64_t>(in, off, endian, value);
        }

      }

      OMETIFFWriter::TIFFState::TIFFState(std::shared_ptr<ome::files::tiff::TIFF>& tiff):
        uuid(boost::uuids::to_string(boost::uuids::random_generator()())),
        tiff(tiff),
        ifdCount(0U)
      {
      }

      OMETIFFWriter::TIFFState::~TIFFState()
      {
      }

      OMETIFFWriter::OMETIFFWriter():
        ome::files::detail::FormatWriter(props),
        logger(ome::common::createLogger("OMETIFFWriter")),
        files(),
        tiffs(),
        currentTIFF(tiffs.end()),
        flags(),
        seriesState(),
        originalMetadataRetrieve(),
        omeMeta(),
        bigTIFF(boost::none)
      {
      }

      OMETIFFWriter::~OMETIFFWriter()
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
      OMETIFFWriter::setId(const boost::filesystem::path& id)
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

        if (currentId && *currentId == canonicalpath)
          return;

        if (seriesState.empty()) // First call to setId.
          {
            baseDir = (canonicalpath.parent_path());

            // Create OME-XML metadata.
            originalMetadataRetrieve = metadataRetrieve;
            omeMeta = std::make_shared<OMEXMLMetadata>();
            convert(*metadataRetrieve, *omeMeta);
            omeMeta->resolveReferences();
            metadataRetrieve = omeMeta;

            // Try to fix up OME-XML metadata if inconsistent.
            if (!validateModel(*omeMeta, false))
              {
                validateModel(*omeMeta, true);
                if (validateModel(*omeMeta, false))
                  {
                    BOOST_LOG_SEV(logger, ome::logging::trivial::warning)
                      << "Correction of model SizeC/ChannelCount/SamplesPerPixel inconsistency attempted";
                  }
                else
                  {
                    BOOST_LOG_SEV(logger, ome::logging::trivial::error)
                      << "Correction of model SizeC/ChannelCount/SamplesPerPixel inconsistency attempted (but inconsistencies remain)";
                  }
              }

            // Set up initial TIFF plane state for all planes in each series.
            dimension_size_type seriesCount = metadataRetrieve->getImageCount();
            seriesState.resize(seriesCount);
            for (dimension_size_type series = 0U; series < seriesCount; ++series)
              {
                dimension_size_type sizeZ = metadataRetrieve->getPixelsSizeZ(series);
                dimension_size_type sizeT = metadataRetrieve->getPixelsSizeT(series);
                dimension_size_type effC = metadataRetrieve->getChannelCount(series);
                dimension_size_type planeCount = sizeZ * sizeT * effC;

                SeriesState& seriesMeta(seriesState.at(series));
                seriesMeta.planes.resize(planeCount);

                for (dimension_size_type plane = 0U; plane < planeCount; ++plane)
                  {
                    detail::OMETIFFPlane& planeMeta(seriesMeta.planes.at(plane));
                    planeMeta.certain = true;
                    planeMeta.status = detail::OMETIFFPlane::ABSENT; // Not written yet.
                  }
              }
          }

        if (flags.empty())
          {
            flags += 'w';

            // Get expected size of pixel data.
            std::shared_ptr<const ::ome::xml::meta::MetadataRetrieve> mr(getMetadataRetrieve());
            storage_size_type pixelSize = significantPixelSize(*mr);

            if (enableBigTIFF(bigTIFF, pixelSize, canonicalpath, logger))
              flags += '8';
          }

        tiff_map::iterator i = tiffs.find(canonicalpath);
        if (i == tiffs.end())
          {
            detail::FormatWriter::setId(canonicalpath);
            std::shared_ptr<ome::files::tiff::TIFF> tiff(ome::files::tiff::TIFF::open(canonicalpath, flags));
            std::pair<tiff_map::iterator,bool> result =
              tiffs.insert(tiff_map::value_type(*currentId, TIFFState(tiff)));
            if (result.second) // should always be true
              currentTIFF = result.first;
            detail::FormatWriter::setId(id);
            setupIFD();
          }
        else
          {
            detail::FormatWriter::setId(i->first);
            currentTIFF = i;
          }
      }

      void
      OMETIFFWriter::close(bool fileOnly)
      {
        try
          {
            if (currentId)
              {
                // Flush last IFD if unwritten.
                if(currentTIFF != tiffs.end())
                  {
                    nextIFD();
                    currentTIFF = tiffs.end();
                  }

                // Remove any BinData and old TiffData elements.
                removeBinData(*omeMeta);
                removeTiffData(*omeMeta);
                // Create UUID and TiffData elements for each series.
                fillMetadata();

                for (auto& tiff : tiffs)
                  {
                    // Get OME-XML for this TIFF file.
                    std::string xml = getOMEXML(tiff.first);
                    // Make sure file is closed before we modify it outside libtiff.
                    tiff.second.tiff->close();

                    // Save OME-XML in the TIFF.
                    saveComment(tiff.first, xml);
                  }
              }

            // Close any open TIFFs.
            for (auto& tiff : tiffs)
              tiff.second.tiff->close();

            files.clear();
            tiffs.clear();
            currentTIFF = tiffs.end();
            flags.clear();
            seriesState.clear();
            originalMetadataRetrieve.reset();
            omeMeta.reset();
            bigTIFF = boost::none;

            ome::files::detail::FormatWriter::close(fileOnly);
          }
        catch (const std::exception&)
          {
            currentTIFF = tiffs.end(); // Ensure we only flush the last IFD once.
            ome::files::detail::FormatWriter::close(fileOnly);
            throw;
          }
      }

      void
      OMETIFFWriter::setSeries(dimension_size_type series)
      {
        const dimension_size_type currentSeries = getSeries();
        detail::FormatWriter::setSeries(series);

        if (currentSeries != series)
          {
            nextIFD();
            setupIFD();
          }
      }

      void
      OMETIFFWriter::setResolution(dimension_size_type resolution)
      {
        const dimension_size_type currentResolution = getResolution();
        detail::FormatWriter::setResolution(resolution);

        if (currentResolution != resolution)
          {
            nextSUBIFD();
            setupIFD();
          }
      }

      void
      OMETIFFWriter::setPlane(dimension_size_type plane)
      {
        const dimension_size_type currentPlane = getPlane();
        detail::FormatWriter::setPlane(plane);

        if (currentPlane != plane)
          {
            nextIFD();
            setupIFD();
          }
      }

      dimension_size_type
      OMETIFFWriter::getTileSizeX() const
      {
        // Get current IFD.  Also requires unset size (fallback) or
        // nonzero set size.
        if (currentId && (!this->tile_size_x ||
                          (this->tile_size_x && *this->tile_size_x)))
          {
            std::shared_ptr<tiff::IFD> ifd (currentTIFF->second.tiff->getCurrentDirectory());
            return ifd->getTileWidth();
          }
        else // setId not called yet; fall back.
          return detail::FormatWriter::getTileSizeX();
      }

      dimension_size_type
      OMETIFFWriter::getTileSizeY() const
      {
        // Get current IFD.  Also requires unset size (fallback) or
        // nonzero set size.
        if (currentId && (!this->tile_size_y ||
                          (this->tile_size_y && *this->tile_size_y)))
          {
            std::shared_ptr<tiff::IFD> ifd (currentTIFF->second.tiff->getCurrentDirectory());
            return ifd->getTileWidth();
          }
        else // setId not called yet; fall back.
          return detail::FormatWriter::getTileSizeY();
      }

      void
      OMETIFFWriter::nextIFD()
      {
        currentTIFF->second.tiff->writeCurrentDirectory();
        ++currentTIFF->second.ifdCount;
      }

      void
      OMETIFFWriter::nextSUBIFD()
      {
        currentTIFF->second.tiff->writeCurrentDirectory();
      }

      void
      OMETIFFWriter::setupIFD()
      {
        // Get current IFD.
        std::shared_ptr<tiff::IFD> ifd (currentTIFF->second.tiff->getCurrentDirectory());

        ifd->setImageWidth(getSizeX());
        ifd->setImageHeight(getSizeY());

        // Default strip or tile size.  We base this upon a default
        // chunk size of 64KiB for greyscale images, which will
        // increase to 192KiB for 3 sample RGB images.  We use strips
        // up to a width of 2048 after which tiles are used.
        if(getSizeX() == 0)
          {
            throw FormatException("Can't set strip or tile size: SizeX is 0");
          }
        else if(!this->tile_size_x && this->tile_size_y)
          {
            // Manually set strip size if the size is positive.  Or
            // else set strips of size 1 as a fallback for
            // compatibility with Bio-Formats.
            if(*this->tile_size_y)
              {
                ifd->setTileType(tiff::STRIP);
                ifd->setTileWidth(getSizeX());
                ifd->setTileHeight(*this->tile_size_y);
              }
            else
              {
                ifd->setTileType(tiff::STRIP);
                ifd->setTileWidth(getSizeX());
                ifd->setTileHeight(1U);
              }
          }
        else if(this->tile_size_x && this->tile_size_y)
          {
            // Manually set tile size if both sizes are positive.  Or
            // else set strips of size 1 as a fallback for
            // compatibility with Bio-Formats.
            if(*this->tile_size_x && *this->tile_size_y)
              {
                ifd->setTileType(tiff::TILE);
                ifd->setTileWidth(*this->tile_size_x);
                ifd->setTileHeight(*this->tile_size_y);
              }
            else
              {
                ifd->setTileType(tiff::STRIP);
                ifd->setTileWidth(getSizeX());
                ifd->setTileHeight(1U);
              }
          }
        else if(getSizeX() < 2048)
          {
            // Default to strips, mainly for compatibility with
            // readers which don't support tiles.
            ifd->setTileType(tiff::STRIP);
            ifd->setTileWidth(getSizeX());
            uint32_t height = 65536U / getSizeX();
            if (height == 0)
              height = 1;
            ifd->setTileHeight(height);
          }
        else
          {
            // Default to tiles.
            ifd->setTileType(tiff::TILE);
            ifd->setTileWidth(256U);
            ifd->setTileHeight(256U);
          }

        std::array<dimension_size_type, 3> coords = getZCTCoords(getPlane());

        dimension_size_type channel = coords[1];

        ifd->setPixelType(getPixelType());
        ifd->setBitsPerSample(bitsPerPixel(getPixelType()));
        ifd->setSamplesPerPixel(getRGBChannelCount(channel));

        const boost::optional<bool> interleaved(getInterleaved());
        if (interleaved && *interleaved)
          ifd->setPlanarConfiguration(tiff::CONTIG);
        else
          ifd->setPlanarConfiguration(tiff::SEPARATE);

        // This isn't necessarily always true; we might want to use a
        // photometric interpretation other than RGB with three
        // samples.
        if (isRGB(channel) && getRGBChannelCount(channel) == 3)
          ifd->setPhotometricInterpretation(tiff::RGB);
        else
          ifd->setPhotometricInterpretation(tiff::MIN_IS_BLACK);

        const boost::optional<std::string> compression(getCompression());
        if(compression)
          ifd->setCompression(tiff::getCodecScheme(*compression));

        if (currentTIFF->second.ifdCount == 0)
          ifd->getField(ome::files::tiff::IMAGEDESCRIPTION).set(default_description);

        // Set up SubIFD if this is a full-resolution image and
        // sub-resolution images are present.
        if (getResolution() == 0)
          {
            ifd->getField(ome::files::tiff::SUBFILETYPE).set(ome::files::tiff::SUBFILETYPE_PAGE);
            if (getResolutionCount() > 1)
              {
                ifd->setSubIFDCount(getResolutionCount() - 1);
              }
          }
        else
          {
            ifd->getField(ome::files::tiff::SUBFILETYPE).set
              (ome::files::tiff::SUBFILETYPE_PAGE|ome::files::tiff::SUBFILETYPE_REDUCEDIMAGE);
          }

        currentIFD = currentTIFF->second.tiff->getCurrentDirectory();
      }

      void
      OMETIFFWriter::saveBytes(dimension_size_type plane,
                               VariantPixelBuffer& buf,
                               dimension_size_type x,
                               dimension_size_type y,
                               dimension_size_type w,
                               dimension_size_type h)
      {
        assertId(currentId, true);

        setPlane(plane);

        // Get plane metadata.
        detail::OMETIFFPlane& planeMeta(seriesState.at(getSeries()).planes.at(plane));

        currentIFD->writeImage(buf, x, y, w, h);

        // Set plane metadata.
        if (getResolution() == 0)
          {
            planeMeta.id = currentTIFF->first;
            planeMeta.index = currentTIFF->second.ifdCount;
            planeMeta.ifd = nullptr; // Unused for writing.
            planeMeta.certain = true;
            planeMeta.status = detail::OMETIFFPlane::PRESENT; // Plane now written.
          }
      }

      void
      OMETIFFWriter::fillMetadata()
      {
        if (!omeMeta)
          throw std::logic_error("OMEXMLMetadata null");

        dimension_size_type badPlanes = 0U;
        for (const auto& series : seriesState)
          for (const auto& plane : series.planes)
            if (plane.status != detail::OMETIFFPlane::PRESENT) // Plane not written.
              ++badPlanes;

        if (badPlanes)
          {
            boost::format fmt
              ("Inconsistent writer state: %1% planes have not been written");
            fmt % badPlanes;
            throw FormatException(fmt.str());
          }

        dimension_size_type seriesCount = getSeriesCount();

        for (dimension_size_type series = 0U; series < seriesCount; ++series)
          {
            DimensionOrder dimOrder = metadataRetrieve->getPixelsDimensionOrder(series);
            dimension_size_type sizeZ = metadataRetrieve->getPixelsSizeZ(series);
            dimension_size_type sizeT = metadataRetrieve->getPixelsSizeT(series);
            dimension_size_type effC = metadataRetrieve->getChannelCount(series);
            dimension_size_type imageCount = sizeZ * sizeT * effC;

            if (imageCount == 0)
              {
                omeMeta->setTiffDataPlaneCount(0, series, 0);
              }

            for (dimension_size_type plane = 0U; plane < imageCount; ++plane)
              {
                std::array<dimension_size_type, 3> coords =
                  ome::files::getZCTCoords(dimOrder, sizeZ, effC, sizeT, imageCount, plane);
                const detail::OMETIFFPlane& planeState(seriesState.at(series).planes.at(plane));

                tiff_map::const_iterator t = tiffs.find(planeState.id);
                if (t != tiffs.end())
                  {
                    path relative(make_relative(baseDir, planeState.id));
                    std::string uuid("urn:uuid:");
                    uuid += t->second.uuid;
                    omeMeta->setUUIDFileName(relative.generic_string(), series, plane);
                    omeMeta->setUUIDValue(uuid, series, plane);

                    // Fill in non-default TiffData attributes.
                    omeMeta->setTiffDataFirstZ(coords[0], series, plane);
                    omeMeta->setTiffDataFirstT(coords[2], series, plane);
                    omeMeta->setTiffDataFirstC(coords[1], series, plane);
                    omeMeta->setTiffDataIFD(planeState.index, series, plane);
                    omeMeta->setTiffDataPlaneCount(1, series, plane);
                  }
                else
                  {
                    boost::format fmt
                      ("Inconsistent writer state: TIFF file %1% not registered with a UUID");
                    fmt % planeState.id;
                    throw FormatException(fmt.str());
                  }
              }
          }
      }

      std::string
      OMETIFFWriter::getOMEXML(const boost::filesystem::path& id)
      {
        tiff_map::const_iterator t = tiffs.find(id);

        if (t == tiffs.end())
          {
            boost::format fmt
              ("Inconsistent writer state: TIFF file %1% not registered with a UUID");
            fmt % id;
            throw FormatException(fmt.str());
          }

        path relative(make_relative(baseDir, id));
        std::string uuid("urn:uuid:");
        uuid += t->second.uuid;
        omeMeta->setUUID(uuid);

        return files::getOMEXML(*omeMeta, true);
      }

      void
      OMETIFFWriter::saveComment(const boost::filesystem::path& id,
                                 const std::string&             xml)
      {
        // Open TIFF as a raw stream.
        boost::iostreams::stream<boost::iostreams::file_descriptor> in(id);
        in.imbue(std::locale::classic());

        // Check endianness.
        EndianType endian = ENDIAN_NATIVE;
        char endianchars[2];
        in >> endianchars[0] >> endianchars[1];

        if (endianchars[0] == 'I' && endianchars[1] == 'I')
          endian = ENDIAN_LITTLE;
        else if (endianchars[0] == 'M' && endianchars[1] == 'M')
          endian = ENDIAN_BIG;
        else
          {
            boost::format fmt
              ("%1% is not a valid TIFF file: Invalid endian header \"%2%%3%\"");
            fmt % id % endianchars[0] % endianchars[1];
            throw FormatException(fmt.str());
          }

        // Check version.
        uint16_t version = read_raw_uint16(in, endian);

        bool bigOffsets;
        if (version == 0x2A)
          bigOffsets = false;
        else if (version == 0x2B)
          bigOffsets = true;
        else
          {
            boost::format fmt
              ("%1% is not a valid TIFF file: Invalid version %2%");
            fmt % id % version;
            throw FormatException(fmt.str());
          }

        // Check offset size and bail out if unusual.
        uint16_t offsetSize = bigOffsets ? read_raw_uint16(in, endian) : 4U;
        if (offsetSize != 4U && offsetSize != 8U)
          {
            boost::format fmt
              ("%1% uses a nonstandard offset size of %2% bytes");
            fmt % id % offsetSize;
            throw FormatException(fmt.str());
          }

        // Get offset of IFD 0 for later use.
        uint64_t ifd0Offset = bigOffsets ? read_raw_uint64(in, 8, endian) : read_raw_uint32(in, 4, endian);

        // Append XML text with a NUL terminator at end of file, noting the offset.
        in.seekp(0, std::ios::end);
        uint64_t descOffset = in.tellp();
        in << xml << '\0';

        // Get number of directory entries for IFD 0.
        uint64_t entries = bigOffsets ? read_raw_uint64(in, ifd0Offset, endian) : read_raw_uint16(in, ifd0Offset, endian);

        // Has ImageDescription been found?
        bool found = false;
        // Loop over directory entries to find ImageDescription.
        for (uint64_t i = 0; i < entries; ++i)
          {
            const uint64_t tagOff = bigOffsets ? ifd0Offset + 8 + (i * 20) : ifd0Offset + 2 + (i * 12);
            const uint16_t tagid = read_raw_uint16(in, tagOff + 0, endian);
            const uint16_t tagtype = read_raw_uint16(in, tagOff + 2, endian);

            if (tagid != TIFFTAG_IMAGEDESCRIPTION)
              continue;
            found = true;

            if (tagtype != TIFF_ASCII)
            {
              boost::format fmt
                ("Invalid TIFF ImageDescription type %1%");
              fmt % tagtype;
              throw FormatException(fmt.str());
            }

            uint64_t count = bigOffsets ? read_raw_uint64(in, tagOff + 4, endian) : read_raw_uint32(in, tagOff + 4, endian);
            if (count != default_description.size() + 1)
              throw FormatException("TIFF ImageDescription size is incorrect");

            // Overwrite count and offset for the ImageDescription text.
            if (bigOffsets)
              {
                write_raw_uint64(in, tagOff + 4, endian, xml.size() + 1);
                write_raw_uint64(in, tagOff + 12, endian, descOffset);
              }
            else
              {
                write_raw_uint32(in, tagOff + 4, endian, xml.size() + 1);
                write_raw_uint32(in, tagOff + 8, endian, descOffset);
              }
          }

        if (!found)
          throw FormatException("Could not find TIFF ImageDescription tag");
        if (!in)
          throw FormatException("Error writing TIFF ImageDescription tag");

        in.close();
      }

      void
      OMETIFFWriter::setBigTIFF(boost::optional<bool> big)
      {
        bigTIFF = big;
      }

      boost::optional<bool>
      OMETIFFWriter::getBigTIFF() const
      {
        return bigTIFF;
      }

    }
  }
}
