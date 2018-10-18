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

#include <boost/range/size.hpp>

#include <ome/files/FormatException.h>
#include <ome/files/in/TIFFReader.h>
#include <ome/files/tiff/IFD.h>
#include <ome/files/tiff/TIFF.h>
#include <ome/files/tiff/Tags.h>
#include <ome/files/tiff/Field.h>
#include <ome/files/tiff/Util.h>

using ome::files::detail::ReaderProperties;
using ome::files::tiff::TIFF;
using ome::files::tiff::IFD;

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
          ReaderProperties p("TIFF", "Tagged Image File Format");
          p.suffixes = {"tif", "tiff", "tf2", "tf8", "btf"};
          p.metadata_levels.insert(MetadataOptions::METADATA_MINIMUM);
          p.metadata_levels.insert(MetadataOptions::METADATA_NO_OVERLAYS);
          p.metadata_levels.insert(MetadataOptions::METADATA_ALL);

          return p;
        }

        const ReaderProperties props(tiff_properties());

        const std::vector<boost::filesystem::path> companion_suffixes = {"txt", "xml"};

      }

      TIFFReader::TIFFReader():
        MinimalTIFFReader(props)
      {
      }

      TIFFReader::~TIFFReader()
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
      TIFFReader::close(bool fileOnly)
      {
        ijmeta = boost::none;

        MinimalTIFFReader::close(fileOnly);
      }

      void
      TIFFReader::readIFDs()
      {
        std::shared_ptr<IFD> ifd0 = *(tiff->begin());

        if (ifd0)
          {
            bool imagej_metadata = true;

            try
              {
                tiff::ImageJMetadata ijmeta(*ifd0);

                auto ijm = tiff::makeCoreMetadata(*ifd0);

                ijm->sizeZ = ijmeta.slices;
                ijm->sizeT = ijmeta.frames;
                ijm->sizeC.clear();
                for (dimension_size_type c = 0; c < ijmeta.channels; ++c)
                  ijm->sizeC.push_back(1U);

                core.clear();
                core.resize(1);
                core[0].emplace_back(std::move(ijm));

                dimension_size_type images = 0;
                for (TIFF::const_iterator i = tiff->begin();
                     i != tiff->end();
                     ++i, ++images)
                  {
                    if (imagej_metadata)
                      {
                        // Verify metadata is consistent

                        std::string desc;
                        (*i)->getField(ome::files::tiff::IMAGEDESCRIPTION).get(desc);
                        std::map<std::string,std::string> imap(tiff::ImageJMetadata::parse_imagedescription(desc));

                        if (imap != ijmeta.map)
                          {
                            std::cerr << "ImageJ TIFF metadata is inconsistent; treating as a plain TIFF";
                            imagej_metadata = false;
                            break;
                          }
                      }
                  }

                if (images != ijmeta.images)
                  {
                    std::cerr << "ImageJ TIFF metadata is inconsistent with TIFF image count; treating as a plain TIFF";
                    imagej_metadata = false;
                  }
              }
            catch (const std::exception& e)
              {
                // Catch all TIFF exceptions and parse failures.
                imagej_metadata = false;
                core.clear();
              }

            if (imagej_metadata)
              this->ijmeta = ijmeta;
          }

        // If a plain TIFF, read metadata from IFDs.
        if (!ijmeta)
          MinimalTIFFReader::readIFDs();
      }

    }
  }
}
