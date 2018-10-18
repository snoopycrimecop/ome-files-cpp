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

#include <iostream>
#include <limits>

#include <ome/files/tiff/Field.h>
#include <ome/files/tiff/IFD.h>
#include <ome/files/tiff/Sentry.h>
#include <ome/files/tiff/TIFF.h>
#include <ome/files/detail/tiff/Tags.h>

#include <tiffio.h>

#include <boost/algorithm/string.hpp>

namespace ome
{
  namespace files
  {
    namespace tiff
    {

      /**
       * Internal implementation details of FieldBase.
       */
      class FieldBase::Impl
      {
      public:
        /// Weak reference to the parent IFD.
        std::weak_ptr<IFD>  ifd;
        /// The tag being wrapped.
        tag_type            tag;
        /// Field information for this tag.
        const ::TIFFField  *fieldinfo;

        /**
         * Constructor.
         *
         * @param ifd the directory the field belongs to.
         * @param tag the tag identifying this field.
         */
        Impl(std::shared_ptr<IFD>& ifd,
             tag_type              tag):
          ifd(ifd),
          tag(tag),
          fieldinfo()
        {
        }

        /**
         * Copy constructor.
         *
         * @param copy the object to copy.
         */
        Impl(const Impl& copy):
          ifd(copy.ifd),
          tag(copy.tag),
          fieldinfo(copy.fieldinfo)
        {
        }

        /// Destructor.
        ~Impl()
        {
        }

        /**
         * Copy assignment operator.
         *
         * @param rhs the object to assign.
         * @returns the modified object.
         */
        Impl&
        operator= (const Impl& rhs)
        {
          ifd = rhs.ifd;
          tag = rhs.tag;
          fieldinfo = rhs.fieldinfo;

          return *this;
        }

        /**
         * Get the directory this field belongs to.
         *
         * @returns the directory.
         */
        std::shared_ptr<IFD>
        getIFD() const
        {
          std::shared_ptr<IFD> sifd = std::shared_ptr<IFD>(ifd);
          if (!sifd)
            throw Exception("Field reference to IFD no longer valid");

          return sifd;
        }

        /**
         * Get the TIFF this field belongs to.
         *
         * @note Needs wrapping in a sentry by the caller.
         *
         * @returns a pointer to the underlying TIFF.
         */
        ::TIFF *
        getTIFF()
        {
          getIFD()->makeCurrent();
          ::TIFF *tiff = reinterpret_cast<::TIFF *>(getIFD()->getTIFF()->getWrapped());
          return tiff;
        }

        /**
         * Get information from libtiff about this field.
         *
         * libtiff is not aware of every tag, or special-purpose
         * private tags.
         *
         * @returns the field information, or null if the tag was
         * unknown.
         */
        const ::TIFFField *
        getFieldInfo()
        {
          if (!fieldinfo)
            {
              Sentry sentry;

              fieldinfo = TIFFFindField(getTIFF(), tag, TIFF_ANY);
              // The returned tag is sometimes incorrect (all libtiff versions)
              if (fieldinfo && tag != TIFFFieldTag(fieldinfo))
                fieldinfo = 0;

              if (!fieldinfo)
                {
                  /**
                   * @todo Log an error or throw an exception.  Having
                   * a logger as a Field class member is likely to
                   * heavyweight.  However, we do need to be sure that
                   * non-standard unregistered fields can be read.
                   * Maybe by using the libtiff custom type
                   * registration as done for ImageJ metadata?
                   */
                  std::cerr << "getFieldInfo: tag " << tag << " unknown\n";
                }
              else
                {
                  // Older libtiff versions allow the same tag to use
                  // multiple datatypes.  Try to find the largest type.
                  const ::TIFFField *larger_info = 0;

                  // Unsigned integer tags.
                  if (TIFFFieldDataType(fieldinfo) == TIFF_SHORT)
                    larger_info = TIFFFindField(getTIFF(), tag, TIFF_LONG);

                  // Signed integer tags.
                  if (TIFFFieldDataType(fieldinfo) == TIFF_SSHORT)
                    larger_info = TIFFFindField(getTIFF(), tag, TIFF_SLONG);

                  if (!larger_info &&
                      (TIFFFieldDataType(fieldinfo) == TIFF_SHORT || TIFFFieldDataType(fieldinfo) == TIFF_LONG))
                    larger_info = TIFFFindField(getTIFF(), tag, TIFF_LONG8);

                  if (!larger_info &&
                      (TIFFFieldDataType(fieldinfo) == TIFF_SSHORT || TIFFFieldDataType(fieldinfo) == TIFF_SLONG))
                    larger_info = TIFFFindField(getTIFF(), tag, TIFF_SLONG8);

                  // IFD.
                  if (TIFFFieldDataType(fieldinfo) == TIFF_IFD)
                    larger_info = TIFFFindField(getTIFF(), tag, TIFF_IFD8);

                  // The returned tag is sometimes incorrect (all libtiff versions)
                  if (larger_info && tag == TIFFFieldTag(larger_info))
                    fieldinfo = larger_info;
                }
            }

          return fieldinfo;
        }
      };

      FieldBase::FieldBase(std::shared_ptr<IFD> ifd,
                           tag_type             tag):
        impl(std::make_unique<Impl>(ifd, tag))
      {
      }

      FieldBase::FieldBase(const FieldBase& copy):
        impl(std::make_unique<Impl>(*(copy.impl)))
      {
      }

      FieldBase::~FieldBase()
      {
      }

      FieldBase&
      FieldBase::operator= (const FieldBase& rhs)
      {
        *impl = *(rhs.impl);
        return *this;
      }

      std::string
      FieldBase::name() const
      {
        std::string ret("Unknown");

        Sentry sentry;

        const ::TIFFField *field = impl->getFieldInfo();
        if (field)
          {
            ret = TIFFFieldName(field);
          }
        else
          {
            std::ostringstream os;
            os << impl->tag;
            ret = os.str();
          }

        return ret;
      }

      Type
      FieldBase::type() const
      {
        Type ret = TYPE_UNDEFINED;

        Sentry sentry;

        const ::TIFFField *field = impl->getFieldInfo();
        if (field)
          ret = static_cast<Type>(TIFFFieldDataType(field));

        return ret;
      }

      bool
      FieldBase::passCount() const
      {
        bool ret = false;

        Sentry sentry;

        const ::TIFFField *field = impl->getFieldInfo();
        if (field)
          ret = TIFFFieldPassCount(field) > 0;

        return ret;
      }

      int
      FieldBase::readCount() const
      {
        int ret = 1;

        Sentry sentry;

        const ::TIFFField *field = impl->getFieldInfo();
        if (field)
          ret = TIFFFieldReadCount(field);

        return ret;
      }

      int
      FieldBase::writeCount() const
      {
        int ret = 1;

        Sentry sentry;

        const ::TIFFField *field = impl->getFieldInfo();
        if (field)
          ret = TIFFFieldWriteCount(field);

        return ret;
      }

      tag_type
      FieldBase::tagNumber() const
      {
        return impl->tag;
      }

      std::shared_ptr<IFD>
      FieldBase::getIFD() const
      {
        return impl->getIFD();
      }

      namespace
      {

        template<typename T>
        void
        generic_get1(std::shared_ptr<IFD> ifd,
                     tag_type             tag,
                     bool                 /* passcount */,
                     int                  /* readcount */,
                     T&                   value)
        {
          ifd->getRawField(tag, &value);
        }

        template<typename T>
        void
        generic_set1(std::shared_ptr<IFD> ifd,
                     tag_type             tag,
                     bool                 /* passcount */,
                     int                  /* writecount */,
                     const T&             value)
        {
          ifd->setRawField(tag, value);
        }

        template<typename T>
        void
        generic_get2(std::shared_ptr<IFD> ifd,
                     tag_type             tag,
                     bool                 /* passcount */,
                     int                  /* readcount */,
                     T&                   value)
        {
          ifd->getRawField(tag, &value[0], &value[1]);
        }

        template<typename T>
        void
        generic_set2(std::shared_ptr<IFD> ifd,
                     tag_type             tag,
                     bool                 /* passcount */,
                     int                  /* writecount */,
                     const T&             value)
        {
          ifd->setRawField(tag, value[0], value[1]);
        }

        template<typename T>
        void
        generic_get3(std::shared_ptr<IFD> ifd,
                     tag_type             tag,
                     bool                 /* passcount */,
                     int                  /* readcount */,
                     T&                   value)
        {
          ifd->getRawField(tag, &value[0], &value[1], &value[2]);
        }

        template<typename T>
        void
        generic_set3(std::shared_ptr<IFD> ifd,
                     tag_type             tag,
                     bool                 /* passcount */,
                     int                  /* writecount */,
                     const T&             value)
        {
          ifd->setRawField(tag, value[0], value[1], value[2]);
        }

        template<typename T>
        void
        generic_get6(std::shared_ptr<IFD> ifd,
                     tag_type             tag,
                     bool                 /* passcount */,
                     int                  /* readcount */,
                     T&                   value)
        {
          ifd->getRawField(tag, &value[0], &value[1], &value[2], &value[3], &value[4], &value[5]);
        }

        template<typename T>
        void
        generic_set6(std::shared_ptr<IFD> ifd,
                     tag_type             tag,
                     bool                 /* passcount */,
                     int                  /* writecount */,
                     const T&             value)
        {
          ifd->setRawField(tag, value[0], value[1], value[2], value[3], value[4], value[5]);
        }

        template<typename T>
        void
        generic_enum16_get1(std::shared_ptr<IFD> ifd,
                            tag_type             tag,
                            Type                 type,
                            bool                 passcount,
                            int                  readcount,
                            T&                   value)
        {
          if (type != TYPE_SHORT &&
              passcount != false &&
              readcount != 1)
            throw Exception("FieldInfo mismatch with Field handler");

          uint16_t v;
          generic_get1(ifd, tag, passcount, readcount, v);
          value = static_cast<T>(v);
        }

        template<typename T>
        void
        generic_enum16_set1(std::shared_ptr<IFD> ifd,
                            tag_type                     tag,
                            Type                         type,
                            bool                         passcount,
                            int                          writecount,
                            const T&                     value)
        {
          if (type != TYPE_SHORT &&
              passcount != false &&
              writecount != 1)
            throw Exception("FieldInfo mismatch with Field handler");

          uint16_t v = static_cast<uint16_t>(value);
          generic_set1(ifd, tag, passcount, writecount, v);
        }

        template<typename T>
        void
        generic_array_get1(std::shared_ptr<IFD> ifd,
                           tag_type             tag,
                           int                  readcount,
                           T&                   value)
        {
          // Special case:
          if (tag == TIFFTAG_IMAGEJ_META_DATA_BYTE_COUNTS ||
              tag == TIFFTAG_IMAGEJ_META_DATA)
            {
              readcount = TIFF_VARIABLE2;
            }
          else if (tag == TIFFTAG_STRIPOFFSETS ||
                   tag == TIFFTAG_STRIPBYTECOUNTS)
            {
              uint16_t pc;
              ifd->getRawField(TIFFTAG_PLANARCONFIG, &pc);
              uint16_t spp;
              ifd->getRawField(TIFFTAG_SAMPLESPERPIXEL, &spp);
              uint32_t ilen;
              ifd->getRawField(TIFFTAG_IMAGELENGTH, &ilen);
              uint32_t rps;
              ifd->getRawField(TIFFTAG_ROWSPERSTRIP, &rps);
              uint32_t spi = static_cast<uint32_t>(std::floor((static_cast<float>(ilen + rps - 1)) / static_cast<float>(rps)));
              if (pc == PLANARCONFIG_CONTIG)
                readcount = static_cast<int>(spi);
              else if (pc == PLANARCONFIG_SEPARATE)
                readcount = static_cast<int>(spp * spi);
            }
          else if (tag == TIFFTAG_TILEOFFSETS ||
                   tag == TIFFTAG_TILEBYTECOUNTS)
            {
              uint16_t pc;
              ifd->getRawField(TIFFTAG_PLANARCONFIG, &pc);
              uint16_t spp;
              ifd->getRawField(TIFFTAG_SAMPLESPERPIXEL, &spp);
              uint32_t ilen, iwid, twid, tlen;
              ifd->getRawField(TIFFTAG_IMAGELENGTH, &ilen);
              ifd->getRawField(TIFFTAG_IMAGEWIDTH, &iwid);
              ifd->getRawField(TIFFTAG_TILELENGTH, &tlen);
              ifd->getRawField(TIFFTAG_TILEWIDTH, &twid);
              uint32_t tacross = (iwid + twid - 1) / twid;
              uint32_t tdown = (ilen + tlen - 1) / tlen;
              uint32_t tpi = tacross * tdown;
              if (pc == PLANARCONFIG_CONTIG)
                readcount = static_cast<int>(tpi);
              else if (pc == PLANARCONFIG_SEPARATE)
                readcount = static_cast<int>(spp * tpi);
            }

          typename T::value_type *valueptr;
          uint32_t count;

          if (readcount == TIFF_SPP)
            {
              uint16_t spp;
              ifd->getRawField(TIFFTAG_SAMPLESPERPIXEL, &spp);
              ifd->getRawField(tag, &valueptr);
              count = static_cast<uint32_t>(spp);
            }
          else if (readcount == TIFF_VARIABLE)
            {
              uint16_t n;
              ifd->getRawField(tag, &n, &valueptr);
              count = static_cast<uint32_t>(n);
            }
          else if (readcount == TIFF_VARIABLE2)
            {
              ifd->getRawField(tag, &count, &valueptr);
            }
          else
            {
              ifd->getRawField(tag, &valueptr);
              count = static_cast<uint32_t>(readcount);
            }

          value = T(valueptr, valueptr + count);
        }

        template<typename T>
        void
        generic_array_set1(std::shared_ptr<IFD> ifd,
                           tag_type             tag,
                           int                  writecount,
                           const T&             value)
        {
          if (writecount == TIFF_SPP)
            {
              uint16_t spp;
              ifd->getRawField(TIFFTAG_SAMPLESPERPIXEL, &spp);
              if (value.size() != spp)
                throw Exception("Field array size does not match SamplesPerPixel");
              ifd->setRawField(tag, value.data());
            }
          else if (writecount == TIFF_VARIABLE)
            {
              if (value.size() > std::numeric_limits<uint16_t>::max())
                throw Exception("Field array size is greater than maximum write count");
              uint16_t n = static_cast<uint16_t>(value.size());
              ifd->setRawField(tag, n, value.data());
            }
          else if (writecount == TIFF_VARIABLE2)
            {
              if (value.size() > std::numeric_limits<uint32_t>::max())
                throw Exception("Field array size is greater than maximum write count");
              ifd->setRawField(tag, value.size(), value.data());
            }
          else
            {
              ifd->setRawField(tag, value.data());
            }
        }

        template<typename T>
        void
        generic_array_get3(std::shared_ptr<IFD> ifd,
                           tag_type             tag,
                           int                  readcount,
                           T&                   value)
        {
          typename T::value_type::value_type *valueptr0, *valueptr1, *valueptr2;
          uint32_t count;
          bool limit = false; // special case for TRANSFERFUNCTION

          // Special case COLORMAP
          if (tag == TIFFTAG_COLORMAP)
            {
              uint16_t bps;
              ifd->getRawField(TIFFTAG_BITSPERSAMPLE, &bps);
              ifd->getRawField(tag, &valueptr0, &valueptr1, &valueptr2);
              count = 1U << bps;
            }
          // Special case TRANSFERFUNCTION
          else if (tag == TIFFTAG_TRANSFERFUNCTION)
            {
              uint16_t spp;
              ifd->getRawField(TIFFTAG_SAMPLESPERPIXEL, &spp);
              uint16_t bps;
              ifd->getRawField(TIFFTAG_BITSPERSAMPLE, &bps);
              uint16_t extrasamples, *ep = 0;
              ifd->getRawFieldDefaulted(TIFFTAG_EXTRASAMPLES, &extrasamples, &ep);

              if ((spp - extrasamples) == 1)
                limit = true;

              if(!limit)
                ifd->getRawField(tag, &valueptr0, &valueptr1, &valueptr2);
              else
                ifd->getRawField(tag, &valueptr0);

              count = 1U << bps;
            }
          else if (readcount == TIFF_SPP)
            {
              uint16_t spp;
              ifd->getRawField(TIFFTAG_SAMPLESPERPIXEL, &spp);
              ifd->getRawField(tag, &valueptr0, &valueptr1, &valueptr2);
              count = static_cast<uint32_t>(spp);
            }
          else if (readcount == TIFF_VARIABLE)
            {
              uint16_t n;
              ifd->getRawField(tag, &n, &valueptr0, &valueptr1, &valueptr2);
              count = static_cast<uint32_t>(n);
            }
          else if (readcount == TIFF_VARIABLE2)
            {
              ifd->getRawField(tag, &count, &valueptr0, &valueptr1, &valueptr2);
            }
          else
            {
              ifd->getRawField(tag, &valueptr0, &valueptr1, &valueptr2);
              count = static_cast<uint32_t>(readcount);
            }

          value.at(0) = typename T::value_type(valueptr0, valueptr0 + count);
          if (!limit)
            {
              value[1] = typename T::value_type(valueptr1, valueptr1 + count);
              value[2] = typename T::value_type(valueptr2, valueptr2 + count);
            }
          else
            {
              value[1].clear();
              value[2].clear();
            }
        }

        template<typename T>
        void
        generic_array_set3(std::shared_ptr<IFD> ifd,
                           tag_type             tag,
                           int                  writecount,
                           const T&             value)
        {
          if (value[0].size() != value[1].size() ||
              value[0].size() != value[2].size())
            throw Exception("Field array sizes are not equal");

          // Special case COLORMAP
          if (tag == TIFFTAG_COLORMAP)
            {
              ifd->setRawField(tag, value[0].data(), value[1].data(), value[2].data());
            }
          // Special case TRANSFERFUNCTION
          else if (tag == TIFFTAG_TRANSFERFUNCTION)
            {
              uint16_t spp;
              ifd->getRawField(TIFFTAG_SAMPLESPERPIXEL, &spp);
              uint16_t extrasamples, *ep = 0;
              ifd->getRawFieldDefaulted(TIFFTAG_EXTRASAMPLES, &extrasamples, &ep);

              if ((spp - extrasamples) > 1)
                ifd->setRawField(tag, value[0].data(), value[1].data(), value[2].data());
              else
                ifd->setRawField(tag, value[0].data());
            }
          else if (writecount == TIFF_SPP)
            {
              uint16_t spp;
              ifd->getRawField(TIFFTAG_SAMPLESPERPIXEL, &spp);
              if (value.size() != spp)
                throw Exception("Field array size does not match SamplesPerPixel");
              ifd->setRawField(tag, value[0].data(), value[1].data(), value[2].data());
            }
          else if (writecount == TIFF_VARIABLE)
            {
              if (value[0].size() > std::numeric_limits<uint16_t>::max())
                throw Exception("Field array size is greater than maximum write count");
              uint16_t n = static_cast<uint16_t>(value.size());
              ifd->setRawField(tag, n, value[0].data(), value[1].data(), value[2].data());
            }
          else if (writecount == TIFF_VARIABLE2)
            {
              if (value[0].size() > std::numeric_limits<uint32_t>::max())
                throw Exception("Field array size is greater than maximum write count");
              ifd->setRawField(tag, value.size(), value[0].data(), value[1].data(), value[2].data());
            }
          else
            {
              ifd->setRawField(tag, value[0].data(), value[1].data(), value[2].data());
            }
        }

        template<typename T>
        void
        generic_enum16_array_get1(std::shared_ptr<IFD> ifd,
                                  tag_type             tag,
                                  int                  readcount,
                                  T&                   value)
        {
          std::vector<uint16_t> v;
          generic_array_get1(ifd, tag, readcount, v);
          value.clear();
          for (auto i : v)
            value.push_back(static_cast<typename T::value_type>(i));
        }

        template<typename T>
        void
        generic_enum16_array_set1(std::shared_ptr<IFD> ifd,
                                  tag_type             tag,
                                  int                  writecount,
                                  const T&             value)
        {
          std::vector<uint16_t> v;
          for(typename T::const_iterator i = value.begin();
              i != value.end();
              ++i)
            v.push_back(static_cast<uint16_t>(*i));
          generic_array_set1(ifd, tag, writecount, v);
        }

      }

      /// @copydoc Field::get()
      template<>
      void
      Field<StringTag1>::get(value_type& value) const
      {
        if (type() != TYPE_ASCII &&
            passCount() != false)
          throw Exception("FieldInfo mismatch with Field handler");

        int rc = readCount();

        if (rc == TIFF_VARIABLE || rc == TIFF_VARIABLE2)
          {
            char *text;
            getIFD()->getRawField(impl->tag, &text);
            value = text;
          }
        else
          {
            std::vector<char> text(static_cast<std::vector<char>::size_type>(rc));
            getIFD()->getRawField(impl->tag, text.data());
            value = std::string(text.begin(), text.end());
          }
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<StringTag1>::set(const value_type& value)
      {
        if (type() != TYPE_ASCII &&
            passCount() != false &&
            writeCount() != TIFF_VARIABLE)
          throw Exception("FieldInfo mismatch with Field handler");

        getIFD()->setRawField(impl->tag, value.c_str());
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<StringTagArray1>::get(value_type& value) const
      {
        if (type() != TYPE_ASCII &&
            passCount() != true)
          throw Exception("FieldInfo mismatch with Field handler");

        const char *text = 0;
        getIFD()->getRawField(impl->tag, text);

        boost::algorithm::split(value, text, boost::is_any_of("\0"), boost::token_compress_on);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<StringTagArray1>::set(const value_type& value)
      {
        if (type() != TYPE_ASCII &&
            passCount() != false &&
            writeCount() != TIFF_VARIABLE)
          throw Exception("FieldInfo mismatch with Field handler");

        std::string s(boost::algorithm::join(value, "\0"));
        // Split value vector into a null-terminated string.
        getIFD()->setRawField(impl->tag, s.c_str());
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<UInt16Tag1>::get(value_type& value) const
      {
        if (type() != TYPE_SHORT &&
            passCount() != false &&
            ((impl->tag != TIFFTAG_BITSPERSAMPLE &&  // broken in libtiff
              impl->tag != TIFFTAG_COMPRESSION &&    // broken in libtiff
              impl->tag != TIFFTAG_DATATYPE &&       // broken in libtiff
              impl->tag != TIFFTAG_MINSAMPLEVALUE && // broken in libtiff
              impl->tag != TIFFTAG_MAXSAMPLEVALUE && // broken in libtiff
              impl->tag != TIFFTAG_SAMPLEFORMAT) &&  // broken in libtiff
             readCount() != 1))
          throw Exception("FieldInfo mismatch with Field handler");

        bool pc = passCount();
        int rc = readCount();

        generic_get1(getIFD(), impl->tag, pc, rc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<UInt16TagArray1>::get(value_type& value) const
      {
        if (impl->tag != TIFFTAG_IMAGEJ_META_DATA_BYTE_COUNTS && // private
            type() != TYPE_SHORT)
          throw Exception("FieldInfo mismatch with Field handler");

        int rc = readCount();

        generic_array_get1(getIFD(), impl->tag, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<UInt16TagArray1>::set(const value_type& value)
      {
        if (impl->tag != TIFFTAG_IMAGEJ_META_DATA_BYTE_COUNTS && // private
            type() != TYPE_SHORT)
          throw Exception("FieldInfo mismatch with Field handler");

        int wc = writeCount();

        generic_array_set1(getIFD(), impl->tag, wc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<UInt16Tag1>::set(const value_type& value)
      {
        if (type() != TYPE_SHORT &&
            passCount() != false &&
            ((impl->tag != TIFFTAG_BITSPERSAMPLE) &&
             writeCount() != 1))
          throw Exception("FieldInfo mismatch with Field handler");

        bool pc = passCount();
        int wc = writeCount();

        generic_set1(getIFD(), impl->tag, pc, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<UInt16Orientation1>::get(value_type& value) const
      {
        bool pc = passCount();
        int rc = readCount();

        generic_enum16_get1(getIFD(), impl->tag, type(), pc, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<UInt16Orientation1>::set(const value_type& value)
      {
        bool pc = passCount();
        int wc = writeCount();

        generic_enum16_set1(getIFD(), impl->tag, type(), pc, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<UInt16PhotometricInterpretation1>::get(value_type& value) const
      {
        bool pc = passCount();
        int rc = readCount();

        generic_enum16_get1(getIFD(), impl->tag, type(), pc, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<UInt16PhotometricInterpretation1>::set(const value_type& value)
      {
        bool pc = passCount();
        int wc = writeCount();

        generic_enum16_set1(getIFD(), impl->tag, type(), pc, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<UInt16PlanarConfiguration1>::get(value_type& value) const
      {
        bool pc = passCount();
        int rc = readCount();

        generic_enum16_get1(getIFD(), impl->tag, type(), pc, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<UInt16PlanarConfiguration1>::set(const value_type& value)
      {
        bool pc = passCount();
        int wc = writeCount();

        generic_enum16_set1(getIFD(), impl->tag, type(), pc, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<UInt16Predictor1>::get(value_type& value) const
      {
        bool pc = passCount();
        int rc = readCount();

        generic_enum16_get1(getIFD(), impl->tag, type(), pc, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<UInt16Predictor1>::set(const value_type& value)
      {
        bool pc = passCount();
        int wc = writeCount();

        generic_enum16_set1(getIFD(), impl->tag, type(), pc, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<UInt16Compression1>::get(value_type& value) const
      {
        bool pc = passCount();
        int rc = readCount();

        generic_enum16_get1(getIFD(), impl->tag, type(), pc, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<UInt16Compression1>::set(const value_type& value)
      {
        bool pc = passCount();
        int wc = writeCount();

        generic_enum16_set1(getIFD(), impl->tag, type(), pc, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<UInt16FillOrder1>::get(value_type& value) const
      {
        bool pc = passCount();
        int rc = readCount();

        generic_enum16_get1(getIFD(), impl->tag, type(), pc, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<UInt16FillOrder1>::set(const value_type& value)
      {
        bool pc = passCount();
        int wc = writeCount();

        generic_enum16_set1(getIFD(), impl->tag, type(), pc, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<UInt16SampleFormat1>::get(value_type& value) const
      {
        bool pc = passCount();
        int rc = readCount();

        generic_enum16_get1(getIFD(), impl->tag, type(), pc, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<UInt16SampleFormat1>::set(const value_type& value)
      {
        bool pc = passCount();
        int wc = writeCount();

        generic_enum16_set1(getIFD(), impl->tag, type(), pc, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<UInt16Threshholding1>::get(value_type& value) const
      {
        bool pc = passCount();
        int rc = readCount();

        generic_enum16_get1(getIFD(), impl->tag, type(), pc, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<UInt16Threshholding1>::set(const value_type& value)
      {
        bool pc = passCount();
        int wc = writeCount();

        generic_enum16_set1(getIFD(), impl->tag, type(), pc, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<UInt16YCbCrPosition1>::get(value_type& value) const
      {
        bool pc = passCount();
        int rc = readCount();

        generic_enum16_get1(getIFD(), impl->tag, type(), pc, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<UInt16YCbCrPosition1>::set(const value_type& value)
      {
        bool pc = passCount();
        int wc = writeCount();

        generic_enum16_set1(getIFD(), impl->tag, type(), pc, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<UInt16Tag2>::get(value_type& value) const
      {
        bool pc = passCount();
        int rc = readCount();

        if (type() != TYPE_SHORT &&
            pc != false &&
            ((impl->tag != BITSPERSAMPLE && rc != TIFF_VARIABLE) &&
             rc != 2))
          throw Exception("FieldInfo mismatch with Field handler");

        generic_get2(getIFD(), impl->tag, pc, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<UInt16Tag2>::set(const value_type& value)
      {
        bool pc = passCount();
        int wc = writeCount();

        if (type() != TYPE_SHORT &&
            pc != false &&
            wc != 2)
          throw Exception("FieldInfo mismatch with Field handler");

        generic_set2(getIFD(), impl->tag, pc, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<UInt16Tag6>::get(value_type& value) const
      {
        bool pc = passCount();
        int rc = readCount();

        if (type() != TYPE_SHORT &&
            pc != false &&
            ((impl->tag != BITSPERSAMPLE && rc != TIFF_VARIABLE) &&
             rc != 6))
          throw Exception("FieldInfo mismatch with Field handler");

        generic_get6(getIFD(), impl->tag, pc, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<UInt16Tag6>::set(const value_type& value)
      {
        bool pc = passCount();
        int wc = writeCount();

        if (type() != TYPE_SHORT &&
            pc != false &&
            wc != 6)
          throw Exception("FieldInfo mismatch with Field handler");

        generic_set6(getIFD(), impl->tag, pc, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<UInt32Tag1>::get(value_type& value) const
      {
        bool pc = passCount();
        int rc = readCount();

        if (type() != TYPE_LONG &&
            pc != false &&
            rc != 1)
          throw Exception("FieldInfo mismatch with Field handler");

        generic_get1(getIFD(), impl->tag, pc, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<UInt32Tag1>::set(const value_type& value)
      {
        bool pc = passCount();
        int wc = writeCount();

        if (type() != TYPE_LONG &&
            pc != false &&
            wc != 1)
          throw Exception("FieldInfo mismatch with Field handler");

        generic_set1(getIFD(), impl->tag, pc, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<FloatTag1>::get(value_type& value) const
      {
        bool pc = passCount();
        int rc = readCount();

        if (type() != TYPE_RATIONAL &&
            pc != false &&
            rc != 1)
          throw Exception("FieldInfo mismatch with Field handler");

        generic_get1(getIFD(), impl->tag, pc, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<FloatTag1>::set(const value_type& value)
      {
        bool pc = passCount();
        int wc = writeCount();

        if (type() != TYPE_RATIONAL &&
            pc != false &&
            wc != 1)
          throw Exception("FieldInfo mismatch with Field handler");

        generic_set1(getIFD(), impl->tag, pc, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<FloatTag2>::get(value_type& value) const
      {
        bool pc = passCount();
        int rc = readCount();

        if (type() != TYPE_RATIONAL &&
            pc != false &&
            rc != 2)
          throw Exception("FieldInfo mismatch with Field handler");

        generic_get2(getIFD(), impl->tag, pc, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<FloatTag2>::set(const value_type& value)
      {
        bool pc = passCount();
        int wc = writeCount();

        if (type() != TYPE_RATIONAL &&
            pc != false &&
            wc != 2)
          throw Exception("FieldInfo mismatch with Field handler");

        generic_set2(getIFD(), impl->tag, pc, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<FloatTag3>::get(value_type& value) const
      {
        bool pc = passCount();
        int rc = readCount();

        if (type() != TYPE_RATIONAL &&
            pc != false &&
            rc != 3)
          throw Exception("FieldInfo mismatch with Field handler");

        generic_get3(getIFD(), impl->tag, pc, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<FloatTag3>::set(const value_type& value)
      {
        bool pc = passCount();
        int wc = writeCount();

        if (type() != TYPE_RATIONAL &&
            pc != false &&
            wc != 3)
          throw Exception("FieldInfo mismatch with Field handler");

        generic_set3(getIFD(), impl->tag, pc, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<FloatTag6>::get(value_type& value) const
      {
        bool pc = passCount();
        int rc = readCount();

        if (type() != TYPE_RATIONAL &&
            pc != false &&
            rc != 6)
          throw Exception("FieldInfo mismatch with Field handler");

        generic_get6(getIFD(), impl->tag, pc, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<FloatTag6>::set(const value_type& value)
      {
        bool pc = passCount();
        int wc = writeCount();

        if (type() != TYPE_RATIONAL &&
            pc != false &&
            wc != 6)
          throw Exception("FieldInfo mismatch with Field handler");

        generic_set6(getIFD(), impl->tag, pc, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<UInt16ExtraSamplesArray1>::get(value_type& value) const
      {
        if (type() != TYPE_SHORT)
          throw Exception("FieldInfo mismatch with Field handler");

        int rc = readCount();

        generic_enum16_array_get1(getIFD(), impl->tag, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<UInt16ExtraSamplesArray1>::set(const value_type& value)
      {
        if (type() != TYPE_SHORT)
          throw Exception("FieldInfo mismatch with Field handler");

        int wc = writeCount();

        generic_enum16_array_set1(getIFD(), impl->tag, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<UInt16TagArray3>::get(value_type& value) const
      {
        if (type() != TYPE_SHORT)
          throw Exception("FieldInfo mismatch with Field handler");

        int rc = readCount();

        generic_array_get3(getIFD(), impl->tag, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<UInt16TagArray3>::set(const value_type& value)
      {
        if (type() != TYPE_SHORT)
          throw Exception("FieldInfo mismatch with Field handler");

        int wc = writeCount();

        generic_array_set3(getIFD(), impl->tag, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<UInt32TagArray1>::get(value_type& value) const
      {
        if (impl->tag != TIFFTAG_IMAGEJ_META_DATA_BYTE_COUNTS && // private
            type() != TYPE_LONG)
          throw Exception("FieldInfo mismatch with Field handler");

        int rc = readCount();

        generic_array_get1(getIFD(), impl->tag, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<UInt32TagArray1>::set(const value_type& value)
      {
        if (impl->tag != TIFFTAG_IMAGEJ_META_DATA_BYTE_COUNTS && // private
            type() != TYPE_LONG)
          throw Exception("FieldInfo mismatch with Field handler");

        int wc = writeCount();

        generic_array_set1(getIFD(), impl->tag, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<UInt64TagArray1>::get(value_type& value) const
      {
        if (type() != TYPE_LONG8 && type() != TYPE_IFD8)
          throw Exception("FieldInfo mismatch with Field handler");

        int rc = readCount();

        generic_array_get1(getIFD(), impl->tag, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<UInt64TagArray1>::set(const value_type& value)
      {
        if (type() != TYPE_LONG8 && type() != TYPE_IFD8)
          throw Exception("FieldInfo mismatch with Field handler");

        int wc = writeCount();

        generic_array_set1(getIFD(), impl->tag, wc, value);
      }

      /// @copydoc Field::get()
      template<>
      void
      Field<RawDataTag1>::get(value_type& value) const
      {
        if (type() != TYPE_BYTE && type() != TYPE_UNDEFINED)
          throw Exception("FieldInfo mismatch with Field handler");

        int rc = readCount();

        generic_array_get1(getIFD(), impl->tag, rc, value);
      }

      /// @copydoc Field::set()
      template<>
      void
      Field<RawDataTag1>::set(const value_type& value)
      {
        if (type() != TYPE_BYTE && type() != TYPE_UNDEFINED)
          throw Exception("FieldInfo mismatch with Field handler");

        int wc = writeCount();

        generic_array_set1(getIFD(), impl->tag, wc, value);
      }

    }
  }
}
