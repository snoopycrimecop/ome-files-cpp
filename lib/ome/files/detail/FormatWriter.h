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

#ifndef OME_FILES_DETAIL_FORMATWRITER_H
#define OME_FILES_DETAIL_FORMATWRITER_H

#include <ome/files/FormatWriter.h>
#include <ome/files/FormatHandler.h>
#include <ome/files/MetadataTools.h>

#include <map>

namespace ome
{
  namespace files
  {
    namespace detail
    {

      /**
       * Properties specific to a particular writer.
       */
      struct WriterProperties
      {
        /// Map of codec to pixel types.
        typedef std::map<ome::xml::model::enums::PixelType,
                         std::set<std::string>> pixel_compression_type_map;

        /// Format name.
        std::string name;
        /// Format description.
        std::string description;
        /// Filename suffixes this format can handle.
        std::vector<boost::filesystem::path> suffixes;
        /// Filename compression suffixes this format can handle.
        std::vector<boost::filesystem::path> compression_suffixes;
        /// Supported compression types.
        std::set<std::string> compression_types;
        /// Supported compression codecs types for each pixel type.
        pixel_compression_type_map pixel_compression_types;
        /// Stacks are supported.
        bool stacks;

        /**
         * Constructor.
         *
         * @param name the format name.
         * @param description a short description of the format.
         */
        WriterProperties(const std::string& name,
                         const std::string& description):
          name(name),
          description(description),
          suffixes(),
          compression_suffixes(),
          compression_types(),
          pixel_compression_types(),
          stacks()
        {
          compression_suffixes.push_back(boost::filesystem::path(""));
        }
      };

      /**
       * Interface for all biological file format writers (default behaviour).
       *
       * @note The @c ColorModel isn't stored here; this is
       * Java-specific and not implemented in C++.
       *
       * @note The current output stream isn't stored here; this is
       * the responsibility of the individual writer.  Having a
       * reference to the base ostream here and keeping this in sync
       * with the derived writer is an unnecessary complication.
       */
      class FormatWriter : public ::ome::files::FormatWriter,
                           virtual public ::ome::files::FormatHandler
      {
      protected:
        /// Writer properties specific to the derived file format.
        const WriterProperties& writerProperties;

        /// The identifier (path) of the currently open file.
        boost::optional<boost::filesystem::path> currentId;

        /// Current output.
        std::shared_ptr<std::ostream> out;

        /// Current series.
        dimension_size_type series;

        /// Current resolution.
        dimension_size_type resolution;

        /// Current plane.
        dimension_size_type plane;

        /// The compression type to use.
        boost::optional<std::string> compression;

        /// Subchannel interleaving enabled.
        boost::optional<bool> interleaved;

        /// Planes are written sequentially.
        bool sequential;

        /// The frames per second to use when writing.
        frame_rate_type framesPerSecond;

        /// Tile size X.
        boost::optional<dimension_size_type> tile_size_x;

        /// Tile size Y.
        boost::optional<dimension_size_type> tile_size_y;

        /**
         * Current metadata store. Should never be accessed directly as the
         * semantics of getMetadataRetrieve() prevent "null" access.
         */
        std::shared_ptr<::ome::xml::meta::MetadataRetrieve> metadataRetrieve;

        /**
         * Current resolution levels.  Set from annotations in
         * metadataRetrieve.
         */
        MetadataList<Resolution> resolutionLevels;

        /// Constructor.
        FormatWriter(const WriterProperties&);

        /// @cond SKIP
        FormatWriter (const FormatWriter&) = delete;

        FormatWriter&
        operator= (const FormatWriter&) = delete;
        /// @endcond SKIP

      public:
        /// Destructor.
        virtual
        ~FormatWriter();

        // Documented in superclass.
        bool
        isThisType(const boost::filesystem::path& name,
                   bool                           open = true) const;

        // Documented in superclass.
        virtual
        dimension_size_type
        getSeriesCount() const;

        // Documented in superclass.
        void
        setLookupTable(dimension_size_type       plane,
                       const VariantPixelBuffer& buf);

        using files::FormatWriter::saveBytes;

        // Documented in superclass.
        void
        saveBytes(dimension_size_type plane,
                  VariantPixelBuffer& buf);

        // Documented in superclass.
        void
        setSeries(dimension_size_type series);

        // Documented in superclass.
        dimension_size_type
        getSeries() const;

        // Documented in superclass.
        void
        setPlane(dimension_size_type plane);

        // Documented in superclass.
        dimension_size_type
        getPlane() const;

        // Documented in superclass.
        bool
        canDoStacks() const;

        // Documented in superclass.
        void
        setMetadataRetrieve(std::shared_ptr<::ome::xml::meta::MetadataRetrieve>& retrieve);

        // Documented in superclass.
        const std::shared_ptr<::ome::xml::meta::MetadataRetrieve>&
        getMetadataRetrieve() const;

        // Documented in superclass.
        std::shared_ptr<::ome::xml::meta::MetadataRetrieve>&
        getMetadataRetrieve();

        // Documented in superclass.
        virtual
        dimension_size_type
        getImageCount() const;

        // Documented in superclass.
        virtual
        bool
        isRGB(dimension_size_type channel) const;

        // Documented in superclass.
        virtual
        dimension_size_type
        getSizeX() const;

        // Documented in superclass.
        virtual
        dimension_size_type
        getSizeY() const;

        // Documented in superclass.
        virtual
        dimension_size_type
        getSizeZ() const;

        // Documented in superclass.
        virtual
        dimension_size_type
        getSizeT() const;

        // Documented in superclass.
        virtual
        dimension_size_type
        getSizeC() const;

        // Documented in superclass.
        virtual
        ome::xml::model::enums::PixelType
        getPixelType() const;

        // Documented in superclass.
        virtual
        pixel_size_type
        getBitsPerPixel() const;

        // Documented in superclass.
        virtual
        dimension_size_type
        getEffectiveSizeC() const;

        // Documented in superclass.
        virtual
        dimension_size_type
        getRGBChannelCount(dimension_size_type channel) const;

        // Documented in superclass.
        virtual
        const std::string&
        getDimensionOrder() const;

        // Documented in superclass.
        virtual
        dimension_size_type
        getIndex(dimension_size_type z,
                 dimension_size_type c,
                 dimension_size_type t) const;

        // Documented in superclass.
        virtual
        std::array<dimension_size_type, 3>
        getZCTCoords(dimension_size_type index) const;

        // Documented in superclass.
        void
        setFramesPerSecond(frame_rate_type rate);

        // Documented in superclass.
        frame_rate_type
        getFramesPerSecond() const;

        // Documented in superclass.
        const std::set<ome::xml::model::enums::PixelType>
        getPixelTypes() const;

        // Documented in superclass.
        const std::set<ome::xml::model::enums::PixelType>
        getPixelTypes(const std::string& codec) const;

        // Documented in superclass.
        bool
        isSupportedType(ome::xml::model::enums::PixelType type) const;

        // Documented in superclass.
        bool
        isSupportedType(ome::xml::model::enums::PixelType type,
                        const std::string&                codec) const;

        // Documented in superclass.
        const std::set<std::string>&
        getCompressionTypes() const;

        const std::set<std::string>&
        getCompressionTypes(ome::xml::model::enums::PixelType type) const;

        // Documented in superclass.
        void
        setCompression(const std::string& compression);

        // Documented in superclass.
        const boost::optional<std::string>&
        getCompression() const;

        // Documented in superclass.
        void
        setInterleaved(bool interleaved);

        // Documented in superclass.
        const boost::optional<bool>&
        getInterleaved() const;

        // Documented in superclass.
        void
        changeOutputFile(const boost::filesystem::path& id);

        // Documented in superclass.
        void
        setWriteSequentially(bool sequential = true);

        // Documented in superclass.
        bool
        getWriteSequentially() const;

        // Documented in superclass.
        void
        setId(const boost::filesystem::path& id);

        // Documented in superclass.
        void
        close(bool fileOnly = false);

        // Documented in superclass.
        const std::string&
        getFormat() const;

        // Documented in superclass.
        const std::string&
        getFormatDescription() const;

        // Documented in superclass.
        const std::vector<boost::filesystem::path>&
        getSuffixes() const;

        // Documented in superclass.
        const std::vector<boost::filesystem::path>&
        getCompressionSuffixes() const;

        // Documented in superclass.
        dimension_size_type
        setTileSizeX(boost::optional<dimension_size_type> size);

        // Documented in superclass.
        dimension_size_type
        getTileSizeX() const;

        // Documented in superclass.
        dimension_size_type
        setTileSizeY(boost::optional<dimension_size_type> size);

        // Documented in superclass.
        dimension_size_type
        getTileSizeY() const;

        // Documented in superclass.
        dimension_size_type
        getResolutionCount() const;

        // Documented in superclass.
        void
        setResolution(dimension_size_type resolution);

        // Documented in superclass.
        dimension_size_type
        getResolution() const;
      };

    }
  }
}

#endif // OME_FILES_DETAIL_FORMATWRITER_H

/*
 * Local Variables:
 * mode:C++
 * End:
 */
