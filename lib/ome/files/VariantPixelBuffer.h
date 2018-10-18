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

#ifndef OME_FILES_VARIANTPIXELBUFFER_H
#define OME_FILES_VARIANTPIXELBUFFER_H

#include <memory>

#include <ome/files/PixelBuffer.h>
#include <ome/files/PixelProperties.h>

#include <ome/compat/variant.h>

// Required with Boost 1.53 after variant include.
#include <boost/preprocessor.hpp>

namespace ome
{
  namespace files
  {

    /**
     * Buffer for all pixel types.
     *
     * The purpose of this class is to allow transfer of pixel data of
     * any type and of any dimensionality.
     *
     * This class uses Boost.Variant to support specializations of
     * PixelBuffer for all combinations of pixel type (excluding
     * endian variants).
     *
     * For high performance access to the pixel data, use of a @c
     * Visitor is recommended.  This has the benefit of generalising
     * the algorithm to operate on all PixelBuffer types, as well as
     * allowing special casing for particular type (e.g. integer vs.
     * float, signed vs. unsigned, simple vs. complex, or any other
     * distinction which affects the algorithm).  This will also allow
     * subsetting of the data if required, again for all pixel types
     * with special casing being possible.
     *
     * @todo Add support for subsetting dimensions.
     */
    class VariantPixelBuffer
    {
    public:

      /// Map a PixelType enum to a shared buffer using a standard language type.
      template<int P>
      using PT = std::shared_ptr<PixelBuffer<typename PixelProperties<P>::std_type>>;

      /// Buffer type, allowing assignment of all buffer types.
      using variant_buffer_type =
        ome::compat::variant<PT<::ome::xml::model::enums::PixelType::INT8>,
                             PT<::ome::xml::model::enums::PixelType::INT16>,
                             PT<::ome::xml::model::enums::PixelType::INT32>,
                             PT<::ome::xml::model::enums::PixelType::UINT8>,
                             PT<::ome::xml::model::enums::PixelType::UINT16>,
                             PT<::ome::xml::model::enums::PixelType::UINT32>,
                             PT<::ome::xml::model::enums::PixelType::BIT>,
                             PT<::ome::xml::model::enums::PixelType::FLOAT>,
                             PT<::ome::xml::model::enums::PixelType::DOUBLE>,
                             PT<::ome::xml::model::enums::PixelType::COMPLEXFLOAT>,
                             PT<::ome::xml::model::enums::PixelType::COMPLEXDOUBLE>>;

      /// Raw pixel type used in public interfaces.
      typedef PixelProperties<::ome::xml::model::enums::PixelType::UINT8>::std_type raw_type;

      /// Size type.
      typedef boost::multi_array_types::size_type size_type;

      /// Type used to index all dimensions in public interfaces.
      typedef std::array<boost::multi_array_types::index, PixelBufferBase::dimensions> indices_type;

      /// Storage ordering type for controlling pixel memory layout.
      typedef PixelBufferBase::storage_order_type storage_order_type;

      /// Extent range type.
      typedef PixelBufferBase::range_type range_type;

    public:
      /**
       * Default constructor.
       *
       * This constructs a pixel buffer of unspecified type, of size 1
       * in all dimensions.  The desired type and size should be set
       * after construction.
       */
      explicit
      VariantPixelBuffer():
        buffer(createBuffer(boost::extents[1][1][1][1]))
      {
      }

      /**
       * Construct from extents (internal storage).
       *
       * Storage for the buffer will be allocated internally.
       *
       * @param extents the extent of each dimension.
       * @param storage the storage ordering, defaulting to C array
       * storage ordering.
       * @param pixeltype the pixel type to store.
       */
      template<class ExtentList>
      explicit
      VariantPixelBuffer(const ExtentList&                   extents,
                         ::ome::xml::model::enums::PixelType pixeltype = ::ome::xml::model::enums::PixelType::UINT8,
                         const storage_order_type&           storage = PixelBufferBase::default_storage_order()):
        buffer(createBuffer(extents, pixeltype, storage))
      {
      }

      /**
       * Construct from ranges (internal storage).
       *
       * Storage for the buffer will be allocated internally.
       *
       * @param range the range of each dimension.
       * @param storage the storage ordering, defaulting to C array
       * storage ordering.
       * @param pixeltype the pixel type to store.
       */
      explicit
      VariantPixelBuffer(const range_type&                   range,
                         ::ome::xml::model::enums::PixelType pixeltype = ::ome::xml::model::enums::PixelType::UINT8,
                         const storage_order_type&           storage = PixelBufferBase::default_storage_order()):
        buffer(createBuffer(range, pixeltype, storage))
      {
      }

      /**
       * Copy constructor.
       *
       * Note that due to the use of shared pointers this only
       * performs a shallow copy.
       *
       * @param buffer the buffer to copy.
       */
      explicit
      VariantPixelBuffer(const VariantPixelBuffer& buffer);

      /**
       * Construct from existing pixel buffer.  Use for referencing external data.
       *
       * @param buffer the buffer to contain.
       */
      template<typename T>
      explicit
      VariantPixelBuffer(std::shared_ptr<PixelBuffer<T>>& buffer):
        buffer(buffer)
      {
      }

      /// Destructor.
      virtual
      ~VariantPixelBuffer()
      {}

      /**
       * Get a reference to the variant buffer.
       *
       * @returns a reference to the buffer.
       */
      variant_buffer_type&
      vbuffer()
      {
        return buffer;
      }

      /**
       * Get a reference to the variant buffer.
       *
       * @returns a reference to the buffer.
       */
      const variant_buffer_type&
      vbuffer() const
      {
        return buffer;
      }

    protected:
      /**
       * Create buffer from extents (helper).
       *
       * Storage for the buffer will be allocated internally.
       *
       * @param extents the extent of each dimension.
       * @param storage the storage ordering, defaulting to C array
       * storage ordering.
       * @param pixeltype the pixel type to store.
       * @returns the new buffer contained in a variant.
       */
      template<class T, class ExtentList>
      static variant_buffer_type
      makeBuffer(const ExtentList&                   extents,
                 const storage_order_type&           storage,
                 ::ome::xml::model::enums::PixelType pixeltype)
      {
        return std::make_shared<PixelBuffer<T>>(extents, pixeltype, ENDIAN_NATIVE, storage);
      }

      /**
       * Create buffer from ranges (helper).
       *
       * Storage for the buffer will be allocated internally.
       *
       * @param range the range of each dimension.
       * @param storage the storage ordering, defaulting to C array
       * storage ordering.
       * @param pixeltype the pixel type to store.
       * @returns the new buffer contained in a variant.
       */
      template<class T>
      static variant_buffer_type
      makeBuffer(const range_type&                   range,
                 const storage_order_type&           storage,
                 ::ome::xml::model::enums::PixelType pixeltype)
      {
        return std::make_shared<PixelBuffer<T>>(range, pixeltype, ENDIAN_NATIVE, storage);
      }

      // No switch default to avoid -Wunreachable-code errors.
      // However, this then makes -Wswitch-default complain.  Disable
      // temporarily.
#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wswitch-default"
#endif

#define OME_FILES_VARIANTPIXELBUFFER_CREATEEXTENTS_CASE(maR, maProperty, maType) \
          case ::ome::xml::model::enums::PixelType::maType:                      \
            buf = makeBuffer<PixelProperties<::ome::xml::model::enums::PixelType::maType>::std_type>(extents, storage, pixeltype); \
            break;

      /**
       * Create buffer from extents (internal storage).
       *
       * Storage for the buffer will be allocated internally.
       *
       * @param extents the extent of each dimension.
       * @param pixeltype the pixel type to store.
       * @param storage the storage ordering, defaulting to C array
       * storage ordering.
       * @returns the new buffer contained in a variant.
       */
      template<class ExtentList>
      static variant_buffer_type
      createBuffer(const ExtentList&                   extents,
                   ::ome::xml::model::enums::PixelType pixeltype = ::ome::xml::model::enums::PixelType::UINT8,
                   const storage_order_type&           storage = PixelBufferBase::default_storage_order())
      {
        variant_buffer_type buf;

        switch(pixeltype)
          {
            BOOST_PP_SEQ_FOR_EACH(OME_FILES_VARIANTPIXELBUFFER_CREATEEXTENTS_CASE, _, OME_XML_MODEL_ENUMS_PIXELTYPE_VALUES);
          }

        return buf;
      }

#undef OME_FILES_VARIANTPIXELBUFFER_CREATEEXTENTS_CASE

#define OME_FILES_VARIANTPIXELBUFFER_CREATERANGE_CASE(maR, maProperty, maType) \
          case ::ome::xml::model::enums::PixelType::maType:                    \
            buf = makeBuffer<PixelProperties<::ome::xml::model::enums::PixelType::maType>::std_type>(range, storage, pixeltype); \
            break;

      /**
       * Create buffer from ranges (helper).
       *
       * Storage for the buffer will be allocated internally.
       *
       * @param range the range of each dimension.
       * @param pixeltype the pixel type to store.
       * @param storage the storage ordering, defaulting to C array
       * storage ordering.
       * @returns the new buffer contained in a variant.
       */
      static variant_buffer_type
      createBuffer(const range_type&                   range,
                   ::ome::xml::model::enums::PixelType pixeltype = ::ome::xml::model::enums::PixelType::UINT8,
                   const storage_order_type&           storage = PixelBufferBase::default_storage_order())
      {
        variant_buffer_type buf;

        switch(pixeltype)
          {
            BOOST_PP_SEQ_FOR_EACH(OME_FILES_VARIANTPIXELBUFFER_CREATERANGE_CASE, _, OME_XML_MODEL_ENUMS_PIXELTYPE_VALUES);
          }

        return buf;
      }

#undef OME_FILES_VARIANTPIXELBUFFER_CREATERANGE_CASE

#ifdef __GNUC__
#  pragma GCC diagnostic pop
#endif

    public:
      /**
       * Set the buffer from extents (helper).
       *
       * Storage for the buffer will be allocated internally.
       *
       * @param extents the extent of each dimension.
       * @param pixeltype the pixel type to store.
       * @param storage the storage ordering, defaulting to C array
       * storage ordering.
       */
      template<class ExtentList>
      void
      setBuffer(const ExtentList&                   extents,
                ::ome::xml::model::enums::PixelType pixeltype = ::ome::xml::model::enums::PixelType::UINT8,
                const storage_order_type&           storage = PixelBufferBase::default_storage_order())
      {
        buffer = createBuffer(extents, pixeltype, storage);
      }

      /**
       * Set the buffer from ranges (internal storage).
       *
       * Storage for the buffer will be allocated internally.
       *
       * @param range the range of each dimension.
       * @param pixeltype the pixel type to store.
       * @param storage the storage ordering, defaulting to C array
       * storage ordering.
       */
      void
      setBuffer(const range_type&                   range,
                ::ome::xml::model::enums::PixelType pixeltype = ::ome::xml::model::enums::PixelType::UINT8,
                const storage_order_type&           storage = PixelBufferBase::default_storage_order())
      {
        buffer = createBuffer(range, pixeltype, storage);
      }

      /**
       * Check if the buffer is internally managed.
       *
       * @returns @c true if the @c MultiArray data is managed
       * internally (i.e. is a @c multi_array) or @c false if not
       * managed (i.e. is a @c multi_array_ref).
       */
      bool
      managed() const;

      /**
       * Get the number of pixel elements in the multi-dimensional array.
       *
       * @returns the number of elements.
       */
      size_type
      num_elements() const;

      /**
       * Get the number of dimensions in the multi-dimensional array.
       *
       * @returns the number of dimensions.
       */
      size_type
      num_dimensions() const;

      /**
       * Get the shape of the multi-dimensional array.
       *
       * The shape is the extent of each array dimension.
       *
       * @returns an array of extents (size is the dimension size).
       */
      const size_type *
      shape() const;

      /**
       * Get the strides of the multi-dimensional array.
       *
       * The strides are the stride associated with each array dimension.
       *
       * @returns an array of strides (size is the dimension size).
       */
      const boost::multi_array_types::index *
      strides() const;

      /**
       * Get the index bases of the multi-dimensional array.
       *
       * The index bases are the numeric index of the first element
       * for each array dimension of the multi-dimensional array.
       *
       * @returns an array of index bases (size is the dimension size).
       */
      const boost::multi_array_types::index *
      index_bases() const;

      /**
       * Get the origin of the array.
       *
       * This is the address of the element at @c
       * [0][0][0][0][0][0][0][0][0].  Note that this is not always
       * the buffer start address, depending upon the dimension
       * ordering.
       *
       * @returns the address of the array origin.
       */
      template <typename T>
      const T *
      origin() const;

      /**
       * Get the array storage order.
       *
       * @returns the storage order.
       */
      const storage_order_type&
      storage_order() const;

      /**
       * Get the type of pixels stored in the buffer.
       *
       * @returns the pixel type.
       */
      ::ome::xml::model::enums::PixelType
      pixelType() const;

      /**
       * Get the endianness of the pixel type stored in the buffer.
       *
       * @returns the endian type.
       */
      EndianType
      endianType() const;

      /**
       * Get the pixel data.
       *
       * @returns the multidimensional pixel data array; this value
       * will never be null.
       * @throws if the contained PixelBuffer is not of the specified
       * type.
       */
      template<typename T>
      typename PixelBuffer<T>::array_ref_type&
      array();

      /**
       * Get the pixel data.
       *
       * @returns the multidimensional pixel data array; this value
       * will never be null.
       * @throws if the contained PixelBuffer is not of the specified
       * type.
       */
      template<typename T>
      const typename PixelBuffer<T>::array_ref_type&
      array() const;

      /**
       * Get raw buffered data.
       *
       * @returns a pointer to the buffer start address.
       */
      raw_type *
      data();

      /**
       * Get raw buffered data.
       *
       * @returns a pointer to the buffer start address.
       */
      const raw_type *
      data() const;

      /**
       * Get raw buffered data.
       *
       * @returns a pointer to the buffer start address.
       * @throws if the contained PixelBuffer is not of the specified
       * type.
       */
      template<typename T>
      T *
      data();

      /**
       * Get raw buffered data.
       *
       * @returns a pointer to the buffer start address.
       * @throws if the contained PixelBuffer is not of the specified
       * type.
       */
      template<typename T>
      const T *
      data() const;

      /**
       * Check the buffer validity.
       *
       * This tests if the @c MultiArray is not null, and hence safe
       * to use.  Note that this is not a guarantee of safety in the
       * case of using an externally managed data buffer, in which
       * case the external buffer must still be valid in addition.
       *
       * @returns @c true if not null, @c false if null.
       */
      bool
      valid() const;

      /**
       * Assign a pixel buffer.
       *
       * The dimension extents must be compatible, but the storage
       * ordering does not.  The buffer contents will be assigned in
       * the logical order rather than the storage order.
       *
       * @param rhs the pixel buffer to assign.
       * @returns the assigned buffer.
       */
      VariantPixelBuffer&
      operator = (const VariantPixelBuffer& rhs);

      /**
       * Compare a pixel buffer for equality.
       *
       * @param rhs the pixel buffer to compare with.
       * @returns @c true if equal, @c false if not equal.
       */
      bool
      operator == (const VariantPixelBuffer& rhs) const;

      /**
       * Compare a pixel buffer for inequality.
       *
       * @param rhs the pixel buffer to compare with.
       * @returns @c true if not equal, @c false if equal.
       */
      bool
      operator != (const VariantPixelBuffer& rhs) const;

      /**
       * Assign pixel values.
       *
       * Note that the range to assign must be equal to num_elements().
       *
       * @param begin the start of the range to assign.
       * @param end the end of the range to assign.
       */
      template <typename InputIterator>
      void
      assign(InputIterator begin,
             InputIterator end);

      /**
       * Read raw pixel data from a stream in physical storage order.
       *
       * Note that the pixels will be read in the physical storage
       * order.  This will typically be a contiguous read, but this is
       * not guaranteed.  The current implementation iterates over
       * each pixel and so may be slower than strictly necessary.
       *
       * @param stream the stream to read from.
       */
      template<class charT, class traits>
      inline void
      read(std::basic_istream<charT,traits>& stream);

      /**
       * Write raw pixel data to a stream in physical storage order.
       *
       * Note that the pixels will be written in the physical storage
       * order.  This will typically be a contiguous read, but this is
       * not guaranteed.  The current implementation iterates over
       * each pixel and so may be slower than strictly necessary.
       *
       * @param stream the stream to write to.
       */
      template<class charT, class traits>
      inline void
      write(std::basic_ostream<charT,traits>& stream) const;

    protected:
      /// Pixel storage.
      variant_buffer_type buffer;
    };

    namespace detail
    {

      /// Find a PixelBuffer data array of a specific pixel type.
      template<typename T>
      struct VariantPixelBufferVisitor
      {
        /**
         * PixelBuffer of correct type.
         *
         * @param v the value to find.
         * @returns a pointer to the data array.
         * @throws if the PixelBuffer is null.
         */
        PixelBuffer<T> *
        operator() (std::shared_ptr<PixelBuffer<T>>& v) const
        {
          if (!v)
            throw std::runtime_error("Null pixel type");
          return v.get();
        }

        /**
         * PixelBuffer of incorrect type.
         *
         * @throws if used.
         * @returns a pointer to the data array.
         */
        template <typename U>
        PixelBuffer<T> *
        operator() (U& /* v */) const
        {
          throw std::runtime_error("Unsupported pixel type conversion for buffer");
        }
      };

      /// Find a PixelBuffer data array of a specific pixel type.
      template<typename T>
      struct VariantPixelBufferConstVisitor
      {
        /**
         * PixelBuffer of correct type.
         *
         * @param v the value to find.
         * @returns a pointer to the data array.
         * @throws if the PixelBuffer is null.
         */
        const PixelBuffer<T> *
        operator() (const std::shared_ptr<PixelBuffer<T>>& v) const
        {
          if (!v)
            throw std::runtime_error("Null pixel type");
          return v.get();
        }

        /**
         * PixelBuffer of incorrect type.
         *
         * @throws if used.
         * @returns a pointer to the data array.
         */
        template <typename U>
        const PixelBuffer<T> *
        operator() (U& /* v */) const
        {
          throw std::runtime_error("Unsupported pixel type conversion for buffer");
        }
      };

      /// Assign a PixelBuffer from an input iterator.
      template <typename InputIterator>
      struct VariantPixelBufferAssignVisitor
      {
        /// Input start.
        InputIterator begin;
        /// Input end.
        InputIterator end;

        /**
         * Constructor.
         *
         * @param begin the start of input.
         * @param end the end of input.
         */
        VariantPixelBufferAssignVisitor(InputIterator begin, InputIterator end):
          begin(begin), end(end)
        {}

        /**
         * PixelBuffer of correct type.
         *
         * @param v the PixelBuffer to assign to.
         * @throws if the PixelBuffer is null or the PixelBuffer's data array is null.
         */
        void
        operator() (std::shared_ptr<PixelBuffer<typename std::iterator_traits<InputIterator>::value_type>>& v) const
        {
          if (!v)
            throw std::runtime_error("Null pixel type");
          v->array().assign(begin, end);
        }

        /**
         * PixelBuffer of incorrect type.
         *
         * @throws if used.
         */
        template <typename T>
        void
        operator() (T& /* v */) const
        {
          throw std::runtime_error("Unsupported pixel type conversion for assignment");
        }
      };

      /// Read data into a PixelBuffer.
      template<class charT, class traits>
      struct VariantPixelBufferReadVisitor
      {
        /// The input stream.
        std::basic_istream<charT,traits>& stream;

        /**
         * Constructor.
         *
         * @param stream the input stream.
         */
        VariantPixelBufferReadVisitor(std::basic_istream<charT,traits>& stream):
          stream(stream)
        {}

        /**
         * Read data into specific PixelBuffer type.
         *
         * @param v the PixelBuffer to use.
         */
        template <typename T>
        void
        operator() (T& v) const
        {
          if (!v)
            throw std::runtime_error("Null pixel type");
          v->read(stream);
        }
      };

      /// Write data from a PixelBuffer.
      template<class charT, class traits>
      struct VariantPixelBufferWriteVisitor
      {
        /// The output stream.
        std::basic_ostream<charT,traits>& stream;

        /**
         * Constructor.
         *
         * @param stream the output stream.
         */
        VariantPixelBufferWriteVisitor(std::basic_ostream<charT,traits>& stream):
          stream(stream)
        {}

        /**
         * Write data from specific PixelBuffer type.
         *
         * @param v the PixelBuffer to use.
         */
        template <typename T>
        void
        operator() (const T& v) const
        {
          if (!v)
            throw std::runtime_error("Null pixel type");
          v->write(stream);
        }
      };

      /// Copy a single sample from a PixelBuffer.
      struct CopySampleVisitor
      {
        /// Destination pixel buffer.
        VariantPixelBuffer& dest;
        /// Sample to copy.
        dimension_size_type sample;

        /**
         * Constructor.
         *
         * @param dest the destination pixel buffer.
         * @param sample the sample to copy.
         */
        CopySampleVisitor(VariantPixelBuffer& dest,
                              dimension_size_type sample):
          dest(dest),
          sample(sample)
        {}

        /**
         * Copy sample.
         *
         * @param v the PixelBuffer to use.
         */
        template<typename T>
        void
        operator()(const T& v)
        {
          // Shape is the same as the source buffer, but with one sample.
          std::array<VariantPixelBuffer::size_type, PixelBufferBase::dimensions> dest_shape;
          const VariantPixelBuffer::size_type *shape_ptr(v->shape());
          std::copy(shape_ptr, shape_ptr + PixelBufferBase::dimensions,
                    dest_shape.begin());
          dest_shape[DIM_SAMPLE] = 1;

          // Default to planar ordering; since openByes/saveBytes
          // don't use ZTC the DimensionOrder doesn't matter here so
          // long as it matches what the TIFF reader/writer uses.
          PixelBufferBase::storage_order_type order(PixelBufferBase::make_storage_order(false));

          /// @todo Only call setBuffer if the shape and pixel type
          /// differ, to allow user control over storage order.
          dest.setBuffer(dest_shape, v->pixelType(), order);

          T& destbuf = ome::compat::get<T>(dest.vbuffer());

          typename boost::multi_array_types::index_gen indices;
          typedef boost::multi_array_types::index_range range;
          destbuf->array() = v->array()[boost::indices[range()][range()][range()][range(sample,sample+1)]];
        }
      };

      /// Merge a single sample into a PixelBuffer.
      struct MergeSampleVisitor
      {
        /// Destination pixel buffer.
        VariantPixelBuffer& dest;
        /// Sample to copy.
        dimension_size_type sample;

        /**
         * Constructor.
         *
         * @param dest the destination pixel buffer.
         * @param sample the sample to copy.
         */
        MergeSampleVisitor(VariantPixelBuffer& dest,
                               dimension_size_type sample):
          dest(dest),
          sample(sample)
        {}

        /**
         * Merge sample.
         *
         * @param v the PixelBuffer to use.
         */
        template<typename T>
        void
        operator()(const T& v)
        {
          T& destbuf = ome::compat::get<T>(dest.vbuffer());

          typename boost::multi_array_types::index_gen indices;
          typedef boost::multi_array_types::index_range range;
          destbuf->array()[boost::indices[range()][range()][range()][range(sample,sample+1)]] = v->array();
        }
      };

    }

    /// @copydoc PixelBuffer::array()
    template<typename T>
    inline typename PixelBuffer<T>::array_ref_type&
    VariantPixelBuffer::array()
    {
      detail::VariantPixelBufferVisitor<T> v;
      return ome::compat::visit(v, buffer)->array();
    }

    /// @copydoc PixelBuffer::array() const
    template<typename T>
    inline const typename PixelBuffer<T>::array_ref_type&
    VariantPixelBuffer::array() const
    {
      detail::VariantPixelBufferConstVisitor<T> v;
      return ome::compat::visit(v, buffer)->array();
    }

    template<typename T>
    inline T *
    VariantPixelBuffer::data()
    {
      detail::VariantPixelBufferVisitor<T> v;
      return ome::compat::visit(v, buffer)->data();
    }

    template<typename T>
    inline const T *
    VariantPixelBuffer::data() const
    {
      detail::VariantPixelBufferConstVisitor<T> v;
      return ome::compat::visit(v, buffer)->data();
    }

    template<typename T>
    inline const T *
    VariantPixelBuffer::origin() const
    {
      detail::VariantPixelBufferConstVisitor<T> v;
      return ome::compat::visit(v, buffer)->origin();
    }

    /**
     * Assign all pixel values from an input iterator.
     *
     * @param begin the start of input.
     * @param end the end of input.
     * @throws if the PixelBuffer is null or the PixelBuffer's data array is null.
     */
    template <typename InputIterator>
    inline void
    VariantPixelBuffer::assign(InputIterator begin,
                               InputIterator end)
    {
      detail::VariantPixelBufferAssignVisitor<InputIterator> v(begin, end);
      ome::compat::visit(v, buffer);
    }

    template<class charT, class traits>
    inline void
    VariantPixelBuffer::read(std::basic_istream<charT,traits>& stream)
    {
      detail::VariantPixelBufferReadVisitor<charT, traits> v(stream);
      ome::compat::visit(v, buffer);
    }

    template<class charT, class traits>
    inline void
    VariantPixelBuffer::write(std::basic_ostream<charT,traits>& stream) const
    {
      detail::VariantPixelBufferWriteVisitor<charT, traits> v(stream);
      ome::compat::visit(v, buffer);
    }

  }
}

namespace std
{

  /**
   * Set VariantPixelBuffer from input stream.
   *
   * @param is the input stream.
   * @param buf the VariantPixelBuffer to set.
   * @returns the input stream.
   */
  template<class charT, class traits>
  inline std::basic_istream<charT,traits>&
  operator>> (std::basic_istream<charT,traits>& is,
              ::ome::files::VariantPixelBuffer& buf)
  {
    buf.read(is);
    return is;
  }

  /**
   * Output VariantPixelBuffer to output stream.
   *
   * @param os the output stream.
   * @param buf the VariantPixelBuffer to output.
   * @returns the output stream.
   */
  template<class charT, class traits>
  inline std::basic_ostream<charT,traits>&
  operator<< (std::basic_ostream<charT,traits>& os,
              const ::ome::files::VariantPixelBuffer& buf)
  {
    buf.write(os);
    return os;
  }

}

#endif // OME_FILES_VARIANTPIXELBUFFER_H

/*
 * Local Variables:
 * mode:C++
 * End:
 */
