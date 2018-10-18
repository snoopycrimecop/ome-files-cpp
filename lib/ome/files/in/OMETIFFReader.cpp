/*
 * #%L
 * OME-FILES C++ library for image IO.
 * Copyright © 2015 Open Microscopy Environment:
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

#include <algorithm>
#include <iterator>
#include <map>
#include <set>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/range/size.hpp>

#include <ome/common/filesystem.h>

#include <ome/files/FormatException.h>
#include <ome/files/FormatTools.h>
#include <ome/files/MetadataTools.h>
#include <ome/files/detail/OMETIFF.h>
#include <ome/files/in/OMETIFFReader.h>
#include <ome/files/tiff/IFD.h>
#include <ome/files/tiff/TIFF.h>
#include <ome/files/tiff/Tags.h>
#include <ome/files/tiff/Field.h>

#include <ome/xml/meta/OMEXMLMetadata.h>
#include <ome/xml/meta/BaseMetadata.h>
#include <ome/xml/meta/Convert.h>

namespace fs = boost::filesystem;
using boost::filesystem::path;
using ome::common::canonical;

using ome::files::detail::ReaderProperties;
using ome::files::tiff::TIFF;
using ome::files::tiff::IFD;

typedef ome::xml::meta::BaseMetadata::index_type index_type;
using namespace ome::xml::model::primitives;
using namespace ome::xml::model::enums;

namespace
{

  struct get_file : public std::unary_function<std::map<std::string, path>::value_type, path>
  {
    path
    operator() (const std::map<std::string, path>::value_type& value) const
    {
      return value.second;
    }
  };

}

namespace ome
{
  namespace files
  {
    namespace in
    {

      namespace
      {

        ReaderProperties
        tiff_properties()
        {
          ReaderProperties p("OME-TIFF",
                             "Open Microscopy Environment TIFF");

          p.suffixes = {"ome.tif",
                        "ome.tiff",
                        "ome.tf2",
                        "ome.tf8",
                        "ome.btf"};
          p.metadata_levels.insert(MetadataOptions::METADATA_MINIMUM);
          p.metadata_levels.insert(MetadataOptions::METADATA_NO_OVERLAYS);
          p.metadata_levels.insert(MetadataOptions::METADATA_ALL);

          return p;
        }

        const ReaderProperties props(tiff_properties());

        const std::vector<path> companion_suffixes{"companion.ome"};

        std::string
        getImageDescription(const TIFF& tiff)
        {
          try
            {
              std::shared_ptr<tiff::IFD> ifd (tiff.getDirectoryByIndex(0));
              if (ifd)
                {
                  std::string omexml;
                  ifd->getField(ome::files::tiff::IMAGEDESCRIPTION).get(omexml);
                  return omexml;
                }
              else
                throw tiff::Exception("No TIFF IFDs found");
            }
          catch (const tiff::Exception&)
            {
              throw FormatException("No TIFF ImageDescription found");
            }
        }

        typedef ome::files::detail::OMETIFFPlane OMETIFFPlane;

        /// OME-TIFF-specific core metadata.
        class OMETIFFMetadata : public CoreMetadata
        {
        public:
          /// Tile width.
          std::vector<dimension_size_type> tileWidth;
          /// Tile width.
          std::vector<dimension_size_type> tileHeight;
          /// Per-plane data.
          std::vector<OMETIFFPlane> tiffPlanes;
          /// SUBIFD index (set for sub-resolutions).
          boost::optional<int> subResolutionOffset;

          OMETIFFMetadata():
            CoreMetadata(),
            tileWidth(),
            tileHeight(),
            tiffPlanes(),
            subResolutionOffset()
          {}

          OMETIFFMetadata(const OMETIFFMetadata& copy):
            CoreMetadata(copy),
            tileWidth(copy.tileWidth),
            tileHeight(copy.tileHeight),
            tiffPlanes(copy.tiffPlanes),
            subResolutionOffset(copy.subResolutionOffset)
          {}

        };

        // Compare if full-resolution and sub-resolution metadata is
        // sufficiently similar to permit use.
        bool
        compareResolution(const CoreMetadata& full,
                          const CoreMetadata& sub)
        {
          return (full.sizeX >= sub.sizeX &&
                  full.sizeY >= sub.sizeY &&
                  full.sizeZ == sub.sizeZ && // Note: change to >= if z reductions are ever supported
                  full.sizeT == sub.sizeT &&
                  full.sizeC == sub.sizeC &&
                  full.pixelType == sub.pixelType &&
                  full.indexed == sub.indexed &&
                  full.interleaved == sub.interleaved);
        }

      }

      OMETIFFReader::OMETIFFReader():
        detail::FormatReader(props),
        logger(ome::common::createLogger("OMETIFFReader")),
        files(),
        invalidFiles(),
        tiffs(),
        metadataFile(),
        usedFiles(),
        hasSPW(false),
        cachedMetadata(),
        cachedMetadataFile()
      {
        this->suffixNecessary = false;
        this->suffixSufficient = false;
        this->domains = getDomainCollection(NON_GRAPHICS_DOMAINS);
        this->companionFiles = true;
        this->datasetDescription = "One or more .ome.tiff files";
      }

      OMETIFFReader::~OMETIFFReader()
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
      OMETIFFReader::close(bool fileOnly)
      {
        if (!fileOnly)
          {
            files.clear();
            invalidFiles.clear();
            cachedMetadataFile.clear();
            cachedMetadata.reset();
            hasSPW = false;
            usedFiles.clear();
            metadataFile.clear();
          }
        tiffs.clear(); // Closes all open TIFFs.

        detail::FormatReader::close(fileOnly);
      }

      bool
      OMETIFFReader::isSingleFile(const boost::filesystem::path& id) const
      {
        if (checkSuffix(id, companion_suffixes))
          return false;

        try
          {
            std::shared_ptr<::ome::xml::meta::Metadata> test_meta(cacheMetadata(id));

            dimension_size_type nImages = 0U;
            for (dimension_size_type i = 0U;
                 i < test_meta->getImageCount();
                 ++i)
              {
                dimension_size_type nChannels = test_meta->getChannelCount(i);
                if (!nChannels)
                  nChannels = 1;
                ome::xml::model::primitives::PositiveInteger z(test_meta->getPixelsSizeZ(i));
                ome::xml::model::primitives::PositiveInteger t(test_meta->getPixelsSizeT(i));

                nImages += static_cast<dimension_size_type>(z) * static_cast<dimension_size_type>(t) * nChannels;
              }

            std::shared_ptr<tiff::TIFF> tiff = TIFF::open(id, "r");

            if (!tiff)
              {
                boost::format fmt("Failed to open ‘%1%’");
                fmt % id.string();
                throw FormatException(fmt.str());
              }

            dimension_size_type nIFD = tiff->directoryCount();

            return nImages > 0 && nImages <= nIFD;
          }
        catch (const std::exception&)
          {
            return FormatReader::isSingleFile(id);
          }
      }

      bool
      OMETIFFReader::isThisType(const boost::filesystem::path& name,
                                bool                           open) const
      {
        if (checkSuffix(name, companion_suffixes))
          return true;

        return detail::FormatReader::isThisType(name, open);
      }

      bool
      OMETIFFReader::isFilenameThisTypeImpl(const boost::filesystem::path& name) const
      {
        bool valid = true;
        try
          {
            std::shared_ptr<::ome::xml::meta::Metadata> test_meta(cacheMetadata(name));
            std::string metadataFile = test_meta->getBinaryOnlyMetadataFile();
            if (!metadataFile.empty())
              {
                // check the suffix to make sure that the MetadataFile is
                // not referencing the current OME-TIFF
                if (checkSuffix(metadataFile, getSuffixes()))
                  {
                    valid = false;
                  }
                else
                  {
                    test_meta = cacheMetadata(metadataFile);
                  }
              }
            if (valid)
              {
                for (::ome::xml::meta::Metadata::index_type i = 0;
                     i < test_meta->getImageCount();
                     ++i)
                  {
                    verifyMinimum(*test_meta, i);
                  }
                if (test_meta->getImageCount() == 0)
                  valid = false;
              }
          }
        catch (const std::exception&)
          {
            valid = FormatReader::isFilenameThisTypeImpl(name);
          }

        if (valid && !isGroupFiles())
          {
            try
              {
                valid = isSingleFile(name);
              }
            catch (const std::exception&)
              {
                valid = false;
              }
          }

        return valid;
      }

      std::shared_ptr<const tiff::IFD>
      OMETIFFReader::ifdAtIndex(dimension_size_type plane) const
      {
        std::shared_ptr<const IFD> ifd;

        const OMETIFFMetadata& ometa(dynamic_cast<const OMETIFFMetadata&>(getCoreMetadata(getSeries(), 0U)));

        if (plane < ometa.tiffPlanes.size())
          {
            const OMETIFFPlane& tiffplane(ometa.tiffPlanes.at(plane));
            std::shared_ptr<const TIFF> tiff(getTIFF(tiffplane.id));
            if (tiff)
              ifd = std::shared_ptr<const IFD>(tiff->getDirectoryByIndex(tiffplane.index));
          }

        if (!ifd)
          {
            boost::format fmt("Failed to open IFD ‘%1%’");
            fmt % plane;
            throw FormatException(fmt.str());
          }

        return ifd;
      }

      const std::vector<std::string>&
      OMETIFFReader::getDomains() const
      {
        assertId(currentId, true);
        return getDomainCollection(hasSPW ? HCS_ONLY_DOMAINS : NON_GRAPHICS_DOMAINS);
      }

      const std::vector<boost::filesystem::path>
      OMETIFFReader::getSeriesUsedFiles(bool noPixels) const
      {
        assertId(currentId, true);

        std::set<boost::filesystem::path> fileSet;

        if (!noPixels)
          {
            if (!metadataFile.empty())
              fileSet.insert(metadataFile);

            const OMETIFFMetadata& ometa(dynamic_cast<const OMETIFFMetadata&>(getCoreMetadata(getSeries(), 0U)));

            for(const auto& plane : ometa.tiffPlanes)
              {
                if (!plane.id.empty())
                  fileSet.insert(plane.id);
              }
          }

        return std::vector<boost::filesystem::path>(fileSet.begin(), fileSet.end());
      }

      FormatReader::FileGroupOption
      OMETIFFReader::fileGroupOption(const std::string& id)
      {
        FileGroupOption group = CAN_GROUP;

        try
          {
            if (!isSingleFile(id))
              group = MUST_GROUP;
          }
        catch (const std::exception&)
          {
          }

        return group;
      }

      dimension_size_type
      OMETIFFReader::getOptimalTileWidth(dimension_size_type channel) const
      {
        assertId(currentId, true);

        const OMETIFFMetadata& ometa(dynamic_cast<const OMETIFFMetadata&>(getCoreMetadata(getSeries(), getResolution())));

        return ometa.tileWidth.at(channel);
      }

      dimension_size_type
      OMETIFFReader::getOptimalTileHeight(dimension_size_type channel) const
      {
        assertId(currentId, true);

        const OMETIFFMetadata& ometa(dynamic_cast<const OMETIFFMetadata&>(getCoreMetadata(getSeries(), getResolution())));

        return ometa.tileHeight.at(channel);
      }

      void
      OMETIFFReader::initFile(const boost::filesystem::path& id)
      {
        detail::FormatReader::initFile(id);

        // Note: Use canonical currentId rather than non-canonical id after this point.
        path dir((*currentId).parent_path());

        if (checkSuffix(*currentId, companion_suffixes))
          {
            initCompanionFile();
            return;
          }

        // Cache and use this TIFF.
        addTIFF(*currentId);
        std::shared_ptr<const TIFF> tiff(getTIFF(*currentId));

        // Get the OME-XML from the first TIFF, and create OME-XML
        // metadata from it.
        std::shared_ptr<::ome::xml::meta::OMEXMLMetadata> meta = cacheMetadata(*currentId);

        std::shared_ptr<::ome::xml::meta::OMEXMLMetadata> companionmeta = readCompanionFile(*meta);
        if (companionmeta)
          meta = companionmeta;

        checkSPW(*meta);

        // Clean up any invalid metadata.
        cleanMetadata(*meta);

        // Retrieve original metadata.
        metadata = getOriginalMetadata(*meta);

        if (!meta->getRoot())
          throw FormatException("Could not parse OME-XML from TIFF ImageDescription");

        // Save image timestamps for later use.
        std::vector<boost::optional<Timestamp>> acquiredDates(meta->getImageCount());
        getAcquisitionDates(*meta, acquiredDates);

        // Get UUID for the first file.
        boost::optional<std::string> currentUUID;
        try
          {
            currentUUID = meta->getUUID();
          }
        catch (const std::exception&)
          {
            // null UUID.
          }

        // Transfer OME-XML metadata to metadata store for reader.
        convert(*meta, *metadataStore, true);

        // Create CoreMetadata for each image.
        index_type seriesCount = meta->getImageCount();
        core.clear();
        core.resize(seriesCount);
        for (index_type i = 0; i < seriesCount; ++i)
          core[i].emplace_back(std::make_unique<OMETIFFMetadata>());

        // UUID → file mapping and used files.
        findUsedFiles(*meta, *currentId, dir, currentUUID);

        // Check that the Channel elements are present and valid.
        checkChannelSamplesPerPixel(*meta);

        // Process TiffData elements.
        findTiffData(*meta);

        // Process Modulo annotations.
        findModulo(*meta);

        // Remove null CoreMetadata entries.
        for (auto& secondary : core)
          {
            std::remove(secondary.begin(), secondary.end(), std::unique_ptr<OMETIFFMetadata>());
          }

        // Workaround for if image count mismatches the image dimensionality.
        fixImageCounts();

        fillMetadata(*metadataStore, *this, false, false);

        fixMissingPlaneIndexes(*meta);

        setAcquisitionDates(acquiredDates);

        // Set the metadata store Pixels.BigEndian attribute to match
        // the values we set in the core metadata
        try
          {
            std::shared_ptr<ome::xml::meta::MetadataRetrieve> metadataRetrieve
              (std::dynamic_pointer_cast<ome::xml::meta::MetadataRetrieve>(getMetadataStore()));

            for (index_type i = 0; i < metadataRetrieve->getImageCount(); ++i)
              {
#ifdef BOOST_BIG_ENDIAN
                metadataStore->setPixelsBigEndian(1, i);
#else // Little endian
                metadataStore->setPixelsBigEndian(0, i);
#endif
              }
          }
        catch(const std::exception&)
          {
            // The metadata store doesn't support getImageCount so we
            // can't meaningfully set anything.
          }

        // Now all image series and TIFF files are discovered, attempt
        // to add sub-resolutions.
        addSubResolutions(*meta);
      }

      void
      OMETIFFReader::initCompanionFile()
      {
        // This is a companion file.  Read the metadata, get the TIFF
        // for the TiffData for the first image, and then recursively
        // call initFile with this file as the id.
        path dir((*currentId).parent_path());
        std::shared_ptr<::ome::xml::meta::OMEXMLMetadata> meta(createOMEXMLMetadata(*currentId));
        path firstTIFF(path(meta->getUUIDFileName(0, 0)));
        close(false); // To force clearing of currentId.
        initFile(canonical(firstTIFF, dir));
      }

      std::shared_ptr<::ome::xml::meta::OMEXMLMetadata>
      OMETIFFReader::readCompanionFile(ome::xml::meta::OMEXMLMetadata& binarymeta)
      {
        path dir((*currentId).parent_path());
        std::shared_ptr<::ome::xml::meta::OMEXMLMetadata> newmeta;

        try
          {
            // Is there an associated binary-only metadata file?
            metadataFile = canonical(path(binarymeta.getBinaryOnlyMetadataFile()), dir);
            if (!metadataFile.empty() && boost::filesystem::exists(metadataFile))
              newmeta = readMetadata(metadataFile);
          }
        catch (const std::exception&)
          {
            /// @todo Log.
            metadataFile.clear();
          }

        return newmeta;
      }

      void OMETIFFReader::checkSPW(ome::xml::meta::OMEXMLMetadata& meta)
      {
        // Is this a screen/plate?
        try
          {
            this->hasSPW = meta.getPlateCount() > 0U;
          }
        catch (const std::exception&)
          {
          }
      }

      void
      OMETIFFReader::findUsedFiles(const ome::xml::meta::OMEXMLMetadata& meta,
                                   const boost::filesystem::path&        currentId,
                                   const boost::filesystem::path&        currentDir,
                                   const boost::optional<std::string>&   currentUUID)
      {
        index_type seriesCount = meta.getImageCount();
        for (index_type series = 0; series < seriesCount; ++series)
          {
            index_type tiffDataCount = meta.getTiffDataCount(series);
            for (index_type td = 0; td < tiffDataCount; ++td)
              {
                std::string uuid;
                path filename;
                try
                  {
                    uuid = meta.getUUIDValue(series, td);
                  }
                catch (const std::exception&)
                  {
                  }
                if (uuid.empty())
                  {
                    // No UUID means that TiffData element refers to this
                    // file.
                    filename = currentId;
                  }
                else
                  {
                    path uuidFilename;
                    try
                      {
                        uuidFilename = meta.getUUIDFileName(series, td);
                        uuidFilename = canonical(uuidFilename, currentDir);
                      }
                    catch (const std::exception&)
                      {
                      }
                    if (fs::exists(uuidFilename))
                      {
                        filename = uuidFilename;
                      }
                    else
                      {
                        if (currentUUID && (uuid == *currentUUID || (*currentUUID).empty()))
                          {
                            // UUID references this file
                            filename = currentId;
                          }
                        else
                          {
                            if (currentUUID)
                              {
                                boost::format fmt("Unmatched filename for UUID ‘%1%’");
                                fmt % uuid;
                                throw FormatException(fmt.str());
                              }
                            else
                              {
                                boost::format fmt("Unmatched filename for UUID ‘%1%’; falling back to current file ‘%2%’ (which lacks a UUID)");
                                fmt % uuid % currentId.string();
                                BOOST_LOG_SEV(logger, ome::logging::trivial::warning) << fmt.str();

                                filename = currentId;
                              }
                          }
                      }
                  }

                std::map<std::string, path>::const_iterator existing = files.find(uuid);
                if (existing == files.end())
                  files.insert(std::make_pair(uuid, filename));
                else if (existing->second != filename)
                  {
                    boost::format fmt("Inconsistent UUID filenames ‘%1%’ and ‘%2%’");
                    fmt % existing->second.string() % filename.string();
                    throw FormatException(fmt.str());
                  }
              }
          }

        // Build list of used files.
        {
          std::set<path> fileSet;
          std::transform(files.begin(), files.end(),
                         std::inserter(fileSet, fileSet.begin()), get_file());
          usedFiles.assign(fileSet.begin(), fileSet.end());
        }
      }

      void
      OMETIFFReader::findTiffData(const ome::xml::meta::OMEXMLMetadata& meta)
      {
        path dir((*currentId).parent_path());
        index_type seriesCount = meta.getImageCount();

        for (index_type series = 0; series < seriesCount; ++series)
          {
            auto& coreMeta = dynamic_cast<OMETIFFMetadata&>(getCoreMetadata(series, 0));

            BOOST_LOG_SEV(logger, ome::logging::trivial::debug)
              << "Image[" << series << "] {";
            BOOST_LOG_SEV(logger, ome::logging::trivial::debug)
              << "  id = " << meta.getImageID(series);

            DimensionOrder order(meta.getPixelsDimensionOrder(series));

            PositiveInteger effSizeC = coreMeta.sizeC.size();
            PositiveInteger sizeT = meta.getPixelsSizeT(series);
            PositiveInteger sizeZ = meta.getPixelsSizeZ(series);
            PositiveInteger num = effSizeC * sizeT * sizeZ;

            coreMeta.tiffPlanes.resize(num);
            index_type tiffDataCount = meta.getTiffDataCount(series);
            boost::optional<NonNegativeInteger> zIndexStart;
            boost::optional<NonNegativeInteger> tIndexStart;
            boost::optional<NonNegativeInteger> cIndexStart;

            seriesIndexStart(meta, series,
                             zIndexStart, tIndexStart, cIndexStart);

            for (index_type td = 0; td < tiffDataCount; ++td)
              {
                BOOST_LOG_SEV(logger, ome::logging::trivial::debug)
                  << "  TiffData[" << td << "] {";

                boost::optional<NonNegativeInteger> tdIFD;
                NonNegativeInteger numPlanes = 0;
                NonNegativeInteger firstZ = 0;
                NonNegativeInteger firstT = 0;
                NonNegativeInteger firstC = 0;

                if (!getTiffDataValues(meta, series, td,
                                       tdIFD, numPlanes,
                                       firstZ, firstT, firstC))
                  break;

                // Note: some writers index FirstC, FirstZ, and FirstT from 1.
                // Subtract index start to correct for this.
                if (cIndexStart && firstC >= *cIndexStart)
                  firstC -= *cIndexStart;
                if (zIndexStart && firstZ >= *zIndexStart)
                  firstZ -= *zIndexStart;
                if (tIndexStart && firstT >= *tIndexStart)
                  firstT -= *tIndexStart;

                if (firstZ >= static_cast<PositiveInteger::value_type>(sizeZ) ||
                    firstC >= static_cast<PositiveInteger::value_type>(effSizeC) ||
                    firstT >= static_cast<PositiveInteger::value_type>(sizeT))
                  {
                    boost::format fmt("Found invalid TiffData: Z=%1%, C=%2%, T=%3%");
                    fmt % firstZ % firstC % firstT;
                    BOOST_LOG_SEV(logger, ome::logging::trivial::warning) << fmt.str();

                    break;
                  }

                dimension_size_type index = ome::files::getIndex(order,
                                                                 sizeZ, effSizeC, sizeT,
                                                                 num,
                                                                 firstZ, firstC, firstT);

                // get reader object for this filename.
                path filename = getTiffDataFilename(meta, series, td);

                addTIFF(filename);

                bool exists = true;
                if (!fs::exists(filename))
                  {
                    // If an absolute filename, try using a relative
                    // name.  Old versions of the Java OMETiffWriter
                    // wrote an absolute path to UUID.FileName, which
                    // causes problems if the file is moved to a
                    // different directory.
                    path relative(dir / filename.filename());
                    if (fs::exists(relative))
                      {
                        filename = relative;
                      }
                    else
                      {
                        filename = *currentId;
                        exists = usedFiles.size() == 1;
                      }
                  }
                if (exists) // check it's really a valid TIFF
                  exists = validTIFF(filename);

                // Fill plane index → IFD mapping
                for (dimension_size_type q = 0;
                     q < static_cast<dimension_size_type>(numPlanes);
                     ++q)
                  {
                    dimension_size_type no = index + q;
                    OMETIFFPlane& plane(coreMeta.tiffPlanes.at(no));
                    plane.id = filename;
                    plane.index = static_cast<dimension_size_type>(*tdIFD) + q;
                    plane.certain = true;
                    plane.status = exists ? OMETIFFPlane::PRESENT : OMETIFFPlane::ABSENT;

                    BOOST_LOG_SEV(logger, ome::logging::trivial::debug)
                      << "    Plane[" << no
                      << "]: file=" << plane.id.string()
                      << ", IFD=" << plane.index;
                  }
                if (numPlanes == 0)
                  {
                    // Unknown number of planes (default value); fill down
                    for (dimension_size_type no = index + 1;
                         no < static_cast<dimension_size_type>(num);
                         ++no)
                      {
                        OMETIFFPlane& plane(coreMeta.tiffPlanes.at(no));
                        if (plane.certain)
                          break;
                        OMETIFFPlane& previousPlane(coreMeta.tiffPlanes.at(no - 1));
                        plane.id = filename;
                        plane.index = previousPlane.index + 1;
                        plane.status = exists ? OMETIFFPlane::PRESENT : OMETIFFPlane::ABSENT;

                        BOOST_LOG_SEV(logger, ome::logging::trivial::debug)
                          << "    Plane[" << no
                          << "]: FILLED";
                      }
                  }
                BOOST_LOG_SEV(logger, ome::logging::trivial::debug)
                  << "  }";
              }

            // Clear any unset planes.
            for (std::vector<OMETIFFPlane>::iterator plane = coreMeta.tiffPlanes.begin();
                 plane != coreMeta.tiffPlanes.end();
                 ++plane)
              {
                if (plane->status != OMETIFFPlane::UNKNOWN)
                  continue;
                plane->id.clear();
                plane->ifd = 0;

                BOOST_LOG_SEV(logger, ome::logging::trivial::debug)
                  << "    Plane[" << plane - coreMeta.tiffPlanes.begin()
                  << "]: CLEARED";
              }

            if (!core.at(series).at(0))
              continue;

            // Verify all planes are available.
            for (dimension_size_type no = 0;
                 no < static_cast<dimension_size_type>(num);
                 ++no)
              {
                OMETIFFPlane& plane(coreMeta.tiffPlanes.at(no));

                BOOST_LOG_SEV(logger, ome::logging::trivial::debug)
                  << "  Verify Plane[" << no
                  << "]: file=" << plane.id.string()
                  << ", IFD=" << plane.index;

                if (plane.id.empty())
                  {
                    BOOST_LOG_SEV(logger, ome::logging::trivial::warning)
                      << "Image ID: " << meta.getImageID(series)
                      << " missing plane #" << no;

                    // Fallback if broken.
                    std::shared_ptr<const TIFF> tiff(getTIFF(*currentId));
                    dimension_size_type nIFD = tiff->directoryCount();

                    coreMeta.tiffPlanes.clear();
                    coreMeta.tiffPlanes.resize(nIFD);
                    for (dimension_size_type p = 0; p < nIFD; ++p)
                      {
                        OMETIFFPlane& plane(coreMeta.tiffPlanes.at(p));
                        plane.id = *currentId;
                        plane.index = p;
                      }
                    break;
                  }
              }

            BOOST_LOG_SEV(logger, ome::logging::trivial::debug)
              << "}";

            // Fill CoreMetadata for full-resolution image.
            fillCoreMetadata(meta, series, 0U);
          }
      }

      boost::filesystem::path
      OMETIFFReader::getTiffDataFilename(const ome::xml::meta::OMEXMLMetadata&    meta,
                                         ome::xml::meta::BaseMetadata::index_type series,
                                         ome::xml::meta::BaseMetadata::index_type tiffDataIndex)
      {
        path dir((*currentId).parent_path());

        boost::optional<path> filename;
        boost::optional<std::string> uuid;

        try
          {
            filename = path(meta.getUUIDFileName(series, tiffDataIndex));
          }
        catch (const std::exception&)
          {
            BOOST_LOG_SEV(logger, ome::logging::trivial::warning)
              << "Ignoring null UUID object when retrieving filename";
          }
        try
          {
            uuid = meta.getUUIDValue(series, tiffDataIndex);
          }
        catch (const std::exception&)
          {
            BOOST_LOG_SEV(logger, ome::logging::trivial::warning)
              << "Ignoring null UUID object when retrieving value";
          }

        if (!filename)
          {
            if (!uuid)
              {
                filename = *currentId;
              }
            else
              {
                std::map<std::string, path>::const_iterator i(files.find(*uuid));
                if (i != files.end())
                  filename = i->second;
                else
                  {
                    boost::format fmt("UUID filename %1% not found; falling back to %2%");
                    fmt % *uuid % *currentId;
                    BOOST_LOG_SEV(logger, ome::logging::trivial::warning) << fmt.str();

                    filename = *currentId;
                  }
              }
          }
        else
          {
            // All the other cases will already have a canonical path.
            if (fs::exists(dir / *filename))
              filename = canonical(dir / *filename, dir);
            else
              {
                invalid_file_map::const_iterator invalid = invalidFiles.find(*filename);
                if (invalid != invalidFiles.end())
                  {
                    filename = invalid->second;
                  }
                else
                  {
                    boost::format fmt("UUID filename %1% not found; falling back to %2%");
                    fmt % *filename % *currentId;
                    BOOST_LOG_SEV(logger, ome::logging::trivial::warning) << fmt.str();

                    invalidFiles.insert(invalid_file_map::value_type(*filename, *currentId));
                    filename = *currentId;
                  }
              }
          }

        return *filename;
      }


      void
      OMETIFFReader::checkChannelSamplesPerPixel(const ome::xml::meta::OMEXMLMetadata& meta)
      {
        index_type seriesCount = meta.getImageCount();

        for (index_type s = 0; s < seriesCount; ++s)
          {
            auto& coreMeta = dynamic_cast<OMETIFFMetadata&>(getCoreMetadata(s, 0));
            dimension_size_type channelCount = meta.getChannelCount(s);
            if (meta.getChannelCount(s) > 0)
              {
                coreMeta.sizeC.clear();
                for (dimension_size_type channel = 0; channel < channelCount; ++channel)
                  {
                    dimension_size_type samplesPerPixel = 1U;
                    try
                      {
                        samplesPerPixel = static_cast<dimension_size_type>(meta.getChannelSamplesPerPixel(s, 0));
                      }
                    catch (const std::exception&)
                      {
                      }
                    coreMeta.sizeC.push_back(samplesPerPixel);
                  }
                // At this stage, assume that the OME-XML
                // channel/samples per pixel data is correct; we'll
                // check this matches later on.
              }
            else // No Channels specified
              {
                dimension_size_type channels = meta.getPixelsSizeC(s);
                coreMeta.sizeC.clear();
                for (dimension_size_type channel = 0; channel < channels; ++channel)
                  coreMeta.sizeC.push_back(1U);

                boost::format fmt("Channel element(s) are missing for series %1%: Falling back to %2% channel(s) of 1 sample each");
                fmt % s % channels;
                BOOST_LOG_SEV(logger, ome::logging::trivial::warning) << fmt.str();
              }
          }
      }

      void
      OMETIFFReader::fillCoreMetadata(const ome::xml::meta::OMEXMLMetadata&    meta,
                                      ome::xml::meta::BaseMetadata::index_type series,
                                      ome::xml::meta::BaseMetadata::index_type resolution)
      {
        auto& coreFullResolutionMeta = dynamic_cast<OMETIFFMetadata&>
          (getCoreMetadata(series, 0));
        auto& coreMeta = dynamic_cast<OMETIFFMetadata&>(getCoreMetadata(series, resolution));

        try
          {
            const OMETIFFPlane& plane(coreFullResolutionMeta.tiffPlanes.at(0));
            std::shared_ptr<const tiff::TIFF> ptiff(getTIFF(plane.id));
            std::shared_ptr<const tiff::IFD> pifd(ptiff->getDirectoryByIndex(plane.index));

            if(!resolution)
              {
                assert (!coreMeta.subResolutionOffset);
              }
            else
              {
                assert (coreMeta.subResolutionOffset);
              }

            if (resolution)
              {
                if (!coreMeta.subResolutionOffset)
                  {
                    boost::format fmt("Sub-resolution offset missing for series %1%, resolution %2%");
                    fmt % series % resolution;
                    throw FormatException(fmt.str());
                  }
                std::vector<uint64_t> subifds;
                pifd->getField(tiff::SUBIFD).get(subifds);
                pifd = ptiff->getDirectoryByOffset(subifds.at(resolution - 1U));
              }

            uint32_t tiffWidth = pifd->getImageWidth();
            uint32_t tiffHeight = pifd->getImageHeight();
            ome::xml::model::enums::PixelType tiffPixelType = pifd->getPixelType();
            tiff::PhotometricInterpretation photometric = pifd->getPhotometricInterpretation();

            auto metaSizeX = meta.getPixelsSizeX(series);
            auto metaSizeY = meta.getPixelsSizeY(series);

            if (resolution == 0 &&
                (metaSizeX != ome::xml::model::primitives::PositiveInteger(tiffWidth) ||
                 metaSizeY != ome::xml::model::primitives::PositiveInteger(tiffHeight)))
              {
                boost::format fmt("Size mismatch: OME=%1%×%2%, TIFF=%3%×%4%");
                fmt % metaSizeX % metaSizeY % tiffWidth % tiffHeight;
                BOOST_LOG_SEV(logger, ome::logging::trivial::warning) << fmt.str();
              }

            coreMeta.sizeX = tiffWidth;
            coreMeta.sizeY = tiffHeight;

            coreMeta.sizeZ = meta.getPixelsSizeZ(series);
            coreMeta.sizeT = meta.getPixelsSizeT(series);
            // coreMeta.sizeC already set
            coreMeta.pixelType = meta.getPixelsType(series);
            coreMeta.imageCount = coreMeta.sizeZ * coreMeta.sizeT * coreMeta.sizeC.size();
            coreMeta.dimensionOrder = meta.getPixelsDimensionOrder(series);
            coreMeta.orderCertain = true;
            // libtiff converts to the native endianess transparently
#ifdef BOOST_BIG_ENDIAN
            coreMeta.littleEndian = false;
#else // Little endian
            coreMeta.littleEndian = true;
#endif

            // This doesn't match the reality, but since samples are
            // addressed as planes this is needed.
            coreMeta.interleaved = (pifd->getPlanarConfiguration() == tiff::CONTIG);

            coreMeta.indexed = false;
            if (photometric == tiff::PALETTE)
              {
                try
                  {
                    std::array<std::vector<uint16_t>, 3> cmap;
                    pifd->getField(ome::files::tiff::COLORMAP).get(cmap);
                    coreMeta.indexed = true;
                  }
                catch (const tiff::Exception&)
                  {
                  }
              }
            coreMeta.metadataComplete = true;
            coreMeta.bitsPerPixel = bitsPerPixel(coreMeta.pixelType);
            try
              {
                pixel_size_type bpp =
                  static_cast<pixel_size_type>(meta.getPixelsSignificantBits(series));
                if (bpp <= coreMeta.bitsPerPixel)
                  {
                    coreMeta.bitsPerPixel = bpp;
                  }
                else
                  {
                    boost::format fmt("BitsPerPixel out of range: OME=%1%, MAX=%2%");
                    fmt % bpp % coreMeta.bitsPerPixel;

                    BOOST_LOG_SEV(logger, ome::logging::trivial::warning) << fmt.str();
                  }
              }
            catch (const std::exception&)
              {
              }

            // Check channel sizes and correct if wrong.
            for (dimension_size_type channel = 0; channel < coreMeta.sizeC.size(); ++channel)
              {
                dimension_size_type planeIndex =
                  ome::files::getIndex(coreMeta.dimensionOrder,
                                       coreMeta.sizeZ,
                                       coreMeta.sizeC.size(),
                                       coreMeta.sizeT,
                                       coreMeta.imageCount,
                                       0,
                                       channel,
                                       0);

                const OMETIFFPlane& plane(coreFullResolutionMeta.tiffPlanes.at(planeIndex));
                std::shared_ptr<const tiff::TIFF> ctiff(getTIFF(plane.id));
                std::shared_ptr<const tiff::IFD> cifd(ctiff->getDirectoryByIndex(plane.index));
                const tiff::TileInfo tinfo(cifd->getTileInfo());
                const dimension_size_type tiffSamples = cifd->getSamplesPerPixel();

                if (coreMeta.sizeC.at(channel) != tiffSamples)
                  {
                    boost::format fmt("SamplesPerPixel mismatch: OME=%1%, TIFF=%2%");
                    fmt % coreMeta.sizeC.at(channel) % tiffSamples;
                    BOOST_LOG_SEV(logger, ome::logging::trivial::warning) << fmt.str();

                    coreMeta.sizeC.at(channel) = tiffSamples;
                  }

                coreMeta.tileWidth.push_back(tinfo.tileWidth());
                coreMeta.tileHeight.push_back(tinfo.tileHeight());
              }

            if (coreMeta.sizeX != tiffWidth)
              {
                boost::format fmt("SizeX mismatch: OME=%1%, TIFF=%2%");
                fmt % coreMeta.sizeX % tiffWidth;

                BOOST_LOG_SEV(logger, ome::logging::trivial::warning) << fmt.str();
              }
            if (coreMeta.sizeY != tiffHeight)
              {
                boost::format fmt("SizeY mismatch: OME=%1%, TIFF=%2%");
                fmt % coreMeta.sizeY % tiffHeight;

                BOOST_LOG_SEV(logger, ome::logging::trivial::warning) << fmt.str();
              }
            if (std::accumulate(coreMeta.sizeC.begin(), coreMeta.sizeC.end(), dimension_size_type(0)) != static_cast<dimension_size_type>(meta.getPixelsSizeC(series)))
              {
                boost::format fmt("SizeC mismatch: Channels=%1%, Pixels=%2%");
                fmt % std::accumulate(coreMeta.sizeC.begin(), coreMeta.sizeC.end(), dimension_size_type(0));
                fmt % meta.getPixelsSizeC(series);

                BOOST_LOG_SEV(logger, ome::logging::trivial::warning) << fmt.str();
              }
            if (coreMeta.pixelType != tiffPixelType)
              {
                boost::format fmt("PixelType mismatch: OME=%1%, TIFF=%2%");
                fmt % coreMeta.pixelType % tiffPixelType;

                BOOST_LOG_SEV(logger, ome::logging::trivial::warning) << fmt.str();
              }
            if (meta.getPixelsBinDataCount(series) > 1U)
              {
                BOOST_LOG_SEV(logger, ome::logging::trivial::warning)
                  << "Ignoring invalid BinData elements in OME-TIFF Pixels element";
              }

            if (resolution == 0)
              {
                fixOMEROMetadata(meta, series);
                fixDimensions(series);
              }
          }
        catch (const std::exception& e)
          {
            boost::format fmt("Incomplete Pixels metadata: %1%");
            fmt % e.what();
            throw FormatException(fmt.str());
          }
      }

      void
      OMETIFFReader::findModulo(const ome::xml::meta::OMEXMLMetadata& meta)
      {
        index_type seriesCount = meta.getImageCount();
        for (index_type series = 0; series < seriesCount; ++series)
          {
            auto& coreMeta = dynamic_cast<OMETIFFMetadata&>(getCoreMetadata(series, 0));

            try
              {
                coreMeta.moduloZ = getModuloAlongZ(meta, series);
              }
            catch (const std::exception&)
              {
              }
            try
              {
                coreMeta.moduloT = getModuloAlongT(meta, series);
              }
            catch (const std::exception&)
              {
              }
            try
              {
                coreMeta.moduloC = getModuloAlongC(meta, series);
              }
            catch (const std::exception&)
              {
              }
          }
      }

      void
      OMETIFFReader::getAcquisitionDates(const ome::xml::meta::OMEXMLMetadata&                                 meta,
                                         std::vector<boost::optional<ome::xml::model::primitives::Timestamp>>& timestamps)
      {
        for (index_type i = 0; i < meta.getImageCount(); ++i)
          {
            boost::optional<Timestamp> ts;
            try
              {
                meta.getImageAcquisitionDate(i);
              }
            catch (const std::exception&)
              {
                // null timestamp.
              }
            timestamps.push_back(ts);
          }
      }

      void
      OMETIFFReader::setAcquisitionDates(const std::vector<boost::optional<ome::xml::model::primitives::Timestamp>>& timestamps)
      {
        for (std::vector<boost::optional<Timestamp>>::const_iterator ts = timestamps.begin();
             ts != timestamps.end();
             ++ts)
          {
            index_type series = std::distance<std::vector<boost::optional<Timestamp>>::const_iterator>(timestamps.begin(), ts);
            if (*ts)
              {
                try
                  {
                    metadataStore->setImageAcquisitionDate(**ts, series);
                  }
                catch (const std::exception& e)
                  {
                    boost::format fmt("Failed to set Image AcquisitionDate for series %1%: %2%");
                    fmt % series % e.what();

                    BOOST_LOG_SEV(logger, ome::logging::trivial::warning) << fmt.str();
                  }
              }
          }
      }

      void
      OMETIFFReader::cleanMetadata(ome::xml::meta::OMEXMLMetadata& meta)
      {
        index_type imageCount = meta.getImageCount();
        for (index_type i = 0; i < imageCount; ++i)
          {
            PositiveInteger sizeC = meta.getPixelsSizeC(i);
            removeChannels(meta, i, sizeC);
          }
      }

      void
      OMETIFFReader::seriesIndexStart(const ome::xml::meta::OMEXMLMetadata&                             meta,
                                      ome::xml::meta::BaseMetadata::index_type                          series,
                                      boost::optional<ome::xml::model::primitives::NonNegativeInteger>& zIndexStart,
                                      boost::optional<ome::xml::model::primitives::NonNegativeInteger>& tIndexStart,
                                      boost::optional<ome::xml::model::primitives::NonNegativeInteger>& cIndexStart)
      {
        // Pre-scan TiffData indices to see if any are indexed from 1.
        index_type tiffDataCount = meta.getTiffDataCount(series);
        for (index_type td = 0; td < tiffDataCount; ++td)
          {
            NonNegativeInteger firstC = 0;
            try
              {
                firstC = meta.getTiffDataFirstC(series, td);
              }
            catch (const std::exception&)
              {
              }
            if (!cIndexStart)
              cIndexStart = firstC;
            else
              cIndexStart = std::min(*cIndexStart, firstC);

            NonNegativeInteger firstZ = 0;
            try
              {
                firstZ = meta.getTiffDataFirstC(series, td);
              }
            catch (const std::exception&)
              {
              }
            if (!zIndexStart)
              zIndexStart = firstZ;
            else
              zIndexStart = std::min(*zIndexStart, firstZ);

            NonNegativeInteger firstT = 0;
            try
              {
                firstT = meta.getTiffDataFirstT(series, td);
              }
            catch (const std::exception&)
              {
              }
            if (!tIndexStart)
              tIndexStart = firstT;
            else
              tIndexStart = std::min(*tIndexStart, firstT);
          }
        if (zIndexStart && *zIndexStart)
          {
            boost::format fmt("Series %1% has non-zero z index start: %2%");
            fmt % series % *zIndexStart;

            BOOST_LOG_SEV(logger, ome::logging::trivial::warning) << fmt.str();
          }
        if (tIndexStart && *tIndexStart)
          {
            boost::format fmt("Series %1% has non-zero t index start: %2%");
            fmt % series % *tIndexStart;

            BOOST_LOG_SEV(logger, ome::logging::trivial::warning) << fmt.str();
          }
        if (cIndexStart && *cIndexStart)
          {
            boost::format fmt("Series %1% has non-zero c index start: %2%");
            fmt % series % *cIndexStart;

            BOOST_LOG_SEV(logger, ome::logging::trivial::warning) << fmt.str();
          }
      }

      bool
      OMETIFFReader::getTiffDataValues(const ome::xml::meta::OMEXMLMetadata&                             meta,
                                       ome::xml::meta::BaseMetadata::index_type                          series,
                                       ome::xml::meta::BaseMetadata::index_type                          tiffData,
                                       boost::optional<ome::xml::model::primitives::NonNegativeInteger>& tdIFD,
                                       ome::xml::model::primitives::NonNegativeInteger&                  numPlanes,
                                       ome::xml::model::primitives::NonNegativeInteger&                  firstZ,
                                       ome::xml::model::primitives::NonNegativeInteger&                  firstT,
                                       ome::xml::model::primitives::NonNegativeInteger&                  firstC)
      {
        bool valid = true;

        try
          {
            tdIFD = meta.getTiffDataIFD(series, tiffData);
          }
        catch (const std::exception&)
          {
          }

        try
          {
            numPlanes = meta.getTiffDataPlaneCount(series, tiffData);
          }
        catch (const std::exception&)
          {
            if (tdIFD)
              numPlanes = 1;
          }

        if (numPlanes == 0)
          {
            core.at(series).at(0) = nullptr;
            valid = false;

            boost::format fmt("Image series %1%, TiffData %2% has zero or missing plane count: Removing invalid image series");
            fmt % series % tiffData;

            BOOST_LOG_SEV(logger, ome::logging::trivial::warning) << fmt.str();
          }

        if (!tdIFD)
          tdIFD = 0; // Start at first IFD in file if unspecified.

        try
          {
            firstC = meta.getTiffDataFirstC(series, tiffData);
          }
        catch (const std::exception&)
          {
          }

        try
          {
            firstT = meta.getTiffDataFirstT(series, tiffData);
          }
        catch (const std::exception&)
          {
          }

        try
          {
            firstZ = meta.getTiffDataFirstZ(series, tiffData);
          }
        catch (const std::exception&)
          {
          }

        return valid;
      }

      void
      OMETIFFReader::fixImageCounts()
      {
        // Unknown why this would occur, because imageCount is
        // computed from the metadata and so should not be possible to
        // be inconsistent…
        for (decltype(core.size()) series = 0; series < core.size(); ++series)
          {
            auto& fullsize = getCoreMetadata(series, 0);

            if (fullsize.imageCount == 1U &&
                (fullsize.sizeZ != 1U ||
                 fullsize.sizeT != 1U ||
                 fullsize.sizeC.size() != 1U))
              {
                boost::format fmt("Correcting image count mismatch for series %1%: Z=%2% T=%3% C=%4% → Z=1 T=1 C=1");
                fmt % series % fullsize.sizeZ % fullsize.sizeT % fullsize.sizeC.size();

                BOOST_LOG_SEV(logger, ome::logging::trivial::warning) << fmt.str();

                fullsize.sizeZ = 1U;
                fullsize.sizeT = 1U;
                // Only one channel, but may contain samples.
                dimension_size_type samples = fullsize.sizeC.at(0);
                fullsize.sizeC.clear();
                fullsize.sizeC.push_back(samples);
              }
          }
      }

      void
      OMETIFFReader::fixMissingPlaneIndexes(ome::xml::meta::OMEXMLMetadata& meta)
      {
        index_type seriesCount = meta.getImageCount();
        for (index_type series = 0; series < seriesCount; ++series)
          {
            index_type planeCount = meta.getPlaneCount(series);
            for (index_type plane = 0; plane < planeCount; ++plane)
              {
                // Make sure that TheZ, TheT and TheC are all set on
                // any existing Planes.  Missing Planes are not added,
                // and existing TheZ, TheC, and TheT values are not
                // changed.
                try
                  {
                    meta.getPlaneTheZ(series, plane);
                  }
                catch (const std::exception&)
                  {
                    metadataStore->setPlaneTheZ(0, series, plane);
                    BOOST_LOG_SEV(logger, ome::logging::trivial::warning)
                      << "Setting unset Plane TheZ value to 0";
                  }

                try
                  {
                    meta.getPlaneTheT(series, plane);
                  }
                catch (const std::exception&)
                  {
                    metadataStore->setPlaneTheT(0, series, plane);
                    BOOST_LOG_SEV(logger, ome::logging::trivial::warning)
                      << "Setting unset Plane TheT value to 0";
                  }

                try
                  {
                    meta.getPlaneTheC(series, plane);
                  }
                catch (const std::exception&)
                  {
                    metadataStore->setPlaneTheC(0, series, plane);
                    BOOST_LOG_SEV(logger, ome::logging::trivial::warning)
                      << "Setting unset Plane TheC value to 0";
                  }
              }
          }
      }

      void
      OMETIFFReader::fixOMEROMetadata(const ome::xml::meta::OMEXMLMetadata&    meta,
                                      ome::xml::meta::BaseMetadata::index_type series)
      {
        // Hackish workaround for files exported by OMERO
        // having an incorrect dimension order.
        {
          std::string uuidFileName;
          try
            {
              if (meta.getTiffDataCount(series) > 0)
                uuidFileName = meta.getUUIDFileName(series, 0);
            }
          catch (const std::exception&)
            {
            }
          if (meta.getChannelCount(series) > 0)
            {
              try
                {
                  // Will throw if null.
                  std::string channelName(meta.getChannelName(series, 0));
                  auto& coreMeta = core.at(series).at(0);
                  if (meta.getTiffDataCount(series) > 0 &&
                      files.find("__omero_export") != files.end() &&
                      coreMeta)
                    coreMeta->dimensionOrder = ome::xml::model::enums::DimensionOrder("XYZCT");
                }
              catch (const std::exception&)
                {
                }
            }
        }
      }

      void
      OMETIFFReader::fixDimensions(ome::xml::meta::BaseMetadata::index_type series)
      {
        auto& coreMeta = core.at(series).at(0);
        if (coreMeta)
          {
            dimension_size_type channelCount = std::accumulate(coreMeta->sizeC.begin(), coreMeta->sizeC.end(), dimension_size_type(0));
            if (coreMeta->sizeZ * coreMeta->sizeT * channelCount > coreMeta->imageCount && // Total image count is greater than imageCount.
                channelCount == coreMeta->sizeC.size()) // No samples, though it's not clear why this matters since they should be accounted for by imageCount.
              {
                if (coreMeta->sizeZ == coreMeta->imageCount)
                  {
                    coreMeta->sizeT = 1U;
                    coreMeta->sizeC.clear();
                    coreMeta->sizeC.push_back(1U);
                  }
                else if (coreMeta->sizeT == coreMeta->imageCount)
                  {
                    coreMeta->sizeZ = 1U;
                    coreMeta->sizeC.clear();
                    coreMeta->sizeC.push_back(1U);
                  }
                else if (channelCount == coreMeta->imageCount)
                  {
                    coreMeta->sizeZ = 1U;
                    coreMeta->sizeT = 1U;
                  }
                else
                  {
                    coreMeta->sizeZ = 1U;
                    coreMeta->sizeT = coreMeta->imageCount;
                    coreMeta->sizeC.clear();
                    coreMeta->sizeC.push_back(1U);
                  }
              }
          }
      }

      void
      OMETIFFReader::addSubResolutions(const ome::xml::meta::OMEXMLMetadata& meta)
      {
        for (dimension_size_type s = 0; s < core.size(); ++s)
          {
            auto& c0 = dynamic_cast<OMETIFFMetadata&>(getCoreMetadata(s, 0));
            const OMETIFFPlane& tiffplane(c0.tiffPlanes.at(0));
            std::shared_ptr<const TIFF> tiff(getTIFF(tiffplane.id));
            if (!tiff)
              continue;
            auto ifd = tiff->getDirectoryByIndex(tiffplane.index);
            std::vector<uint64_t> subifds;
            try
              {
                try
                  {
                    ifd->getField(tiff::SUBIFD).get(subifds);
                  }
                catch (const ome::files::tiff::Exception&)
                  {
                    // No sub-resolutions exist
                    continue;
                  }

                // Resize core metadata to include full image and all sub-resolutions.
                core.at(s).resize(1U + subifds.size());
                for (index_type i = 1; i < core.at(s).size(); ++i)
                  {
                    core.at(s).at(i) = std::make_unique<OMETIFFMetadata>();
                  }

                for (dimension_size_type r = 0; r < subifds.size(); ++r)
                  {
                    auto& cr = dynamic_cast<OMETIFFMetadata&>(getCoreMetadata(s, 1U + r));
                    cr.subResolutionOffset = r;
                    // checkChannelSamplesPerPixel not used for
                    // sub-resolutions; could be refactored into
                    // fillCoreMetadata to work for all resolution
                    // levels.
                    cr.sizeC = c0.sizeC;
                    // Fill CoreMetadata for full-resolution image.
                    fillCoreMetadata(meta, s, 1U + r);

                    if (!compareResolution(*(core.at(s).at(0)), *(core.at(s).at(1U + r))))
                      {
                        boost::format fmt("Sub-resolution core metadata mismatch with full resolution core metadata: series %1%, resolution %2%");
                        fmt % s % (1U + r);
                        throw FormatException(fmt.str());
                      }
                  }
              }
            catch (const std::exception& e)
              {
                // Something was wrong with the sub-resolution images; discard them.
                boost::format fmt("Failed to get sub-resolutions for series %1%: %2%");
                fmt % s % e.what();

                BOOST_LOG_SEV(logger, ome::logging::trivial::warning) << fmt.str();

                continue;
              }
          }
        orderResolutions(core);
      }

      void
      OMETIFFReader::getLookupTable(dimension_size_type plane,
                                    VariantPixelBuffer& buf) const
      {
        assertId(currentId, true);

        setPlane(plane);

        std::shared_ptr<const IFD> ifd(ifdAtIndex(plane));

        try
          {
            ifd->readLookupTable(buf);
          }
        catch (const std::exception& e)
          {
            boost::format fmt("Failed to get lookup table:");
            fmt % e.what();
            throw FormatException(fmt.str());
          }
      }

      void
      OMETIFFReader::openBytesImpl(dimension_size_type plane,
                                   VariantPixelBuffer& buf,
                                   dimension_size_type x,
                                   dimension_size_type y,
                                   dimension_size_type w,
                                   dimension_size_type h) const
      {
        assertId(currentId, true);

        std::shared_ptr<const IFD> ifd(ifdAtIndex(plane));

        if (resolution)
          {
            const OMETIFFMetadata& ometa(dynamic_cast<const OMETIFFMetadata&>(getCoreMetadata(getSeries(), getResolution())));
            if (!ometa.subResolutionOffset)
              {
                boost::format fmt("Sub-resolution offset missing for series %1%, resolution %2%");
                fmt % series % resolution;
                throw FormatException(fmt.str());
              }

            std::shared_ptr<TIFF> tiff = ifd->getTIFF();

            std::vector<uint64_t> subifds;
            ifd->getField(tiff::SUBIFD).get(subifds);
            ifd = tiff->getDirectoryByOffset(subifds.at(*(ometa.subResolutionOffset)));
          }

        ifd->readImage(buf, x, y, w, h);
      }

      void
      OMETIFFReader::addTIFF(const boost::filesystem::path& tiff)
      {
        tiffs.insert(std::make_pair(tiff, std::shared_ptr<tiff::TIFF>()));
      }

      std::shared_ptr<const ome::files::tiff::TIFF>
      OMETIFFReader::getTIFF(const boost::filesystem::path& tiff) const
      {
        tiff_map::iterator i = tiffs.find(tiff);

        if (i == tiffs.end())
          {
            BOOST_LOG_SEV(logger, ome::logging::trivial::warning)
              << "Failed to find cached TIFF " << i->first.string();
            boost::format fmt("Failed to find cached TIFF ‘%1%’");
            fmt % i->first.string();
            throw FormatException(fmt.str());
          }

        // second.second is the validity if the TIFF is null.  false
        // is uninitialised; true is invalid.  Used to prevent
        // repeated initialisation when the file is broken or
        // nonexistent.
        if (!i->second)
          {
            try
              {
                i->second = tiff::TIFF::open(i->first, "r");
              }
            catch (const ome::files::tiff::Exception&)
              {
              }
          }

        if (!i->second)
          {
            BOOST_LOG_SEV(logger, ome::logging::trivial::warning)
              << "Failed to open TIFF " << i->first.string();
            boost::format fmt("Failed to open ‘%1%’");
            fmt % i->first.string();
            throw FormatException(fmt.str());
          }

        return i->second;
      }

      bool
      OMETIFFReader::validTIFF(const boost::filesystem::path& tiff) const
      {
        std::shared_ptr<const ome::files::tiff::TIFF> valid(getTIFF(tiff));
        return static_cast<bool>(valid);
      }

      void
      OMETIFFReader::closeTIFF(const boost::filesystem::path& tiff)
      {
        tiff_map::iterator i = tiffs.find(tiff);
        if (i->second)
          {
            i->second->close();
            i->second = std::shared_ptr<ome::files::tiff::TIFF>();
          }
      }

      std::shared_ptr<::ome::xml::meta::OMEXMLMetadata>
      OMETIFFReader::readMetadata(const ome::files::tiff::TIFF& tiff)
      {
        return createOMEXMLMetadata(getImageDescription(tiff));
      }

      std::shared_ptr<::ome::xml::meta::OMEXMLMetadata>
      OMETIFFReader::readMetadata(const boost::filesystem::path& id)
      {
        if (!checkSuffix(id, companion_suffixes))
          {
            addTIFF(id);
            std::shared_ptr<const TIFF> tiff(getTIFF(id));
            return createOMEXMLMetadata(getImageDescription(*tiff));
          }
        else
          {
            return createOMEXMLMetadata(id);
          }
      }

      std::shared_ptr<::ome::xml::meta::OMEXMLMetadata>
      OMETIFFReader::cacheMetadata(const boost::filesystem::path& id) const
      {
        std::shared_ptr<::ome::xml::meta::OMEXMLMetadata> meta;
        path dir(id.parent_path());
        if(canonical(id, dir) == cachedMetadataFile && cachedMetadata)
          {
            meta = cachedMetadata; // reuse cached metadata
          }
        else
          {
            std::shared_ptr<tiff::TIFF> tiff = TIFF::open(id, "r");

            if (!tiff)
              {
                boost::format fmt("Failed to open ‘%1%’");
                fmt % id.string();
                throw FormatException(fmt.str());
              }

            std::string omexml(getImageDescription(*tiff));

            // Basic sanity check before parsing.
            std::string::size_type lpos = omexml.find_last_not_of(" \r\n\t\f\v");
            if (omexml.size() == 0 ||
                omexml[0] != '<' ||
                lpos == std::string::npos ||
                omexml[lpos] != '>')
              {
                boost::format fmt("Badly formed or invalid XML document in ‘%1%’");
                fmt % id.string();
                throw FormatException(fmt.str());
              }

            meta = createOMEXMLMetadata(omexml);

            // Don't overwrite state for open readers
            cachedMetadata = meta;
            cachedMetadataFile = canonical(id, dir);
          }

        return meta;
      }

      std::shared_ptr<ome::xml::meta::MetadataStore>
      OMETIFFReader::getMetadataStoreForConversion()
      {
        return getMetadataStore();
      }

      std::shared_ptr<ome::xml::meta::MetadataStore>
      OMETIFFReader::getMetadataStoreForDisplay()
      {
        return getMetadataStore();
      }

    }
  }
}
