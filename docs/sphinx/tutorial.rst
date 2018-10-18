.. _ome-files-cpp-tutorial:

Tutorial
========

.. _tutorial_units:

Units of measurement
--------------------

Many of the metadata interfaces provide methods to get or set values
with an associated unit of measurement.  The reason for this is to
ensure that values are always associated with an appropriate unit, and
to enforce compile-time or run-time sanity checks to ensure that the
correct unit type is used, and that any unit conversions performed are
legal.  The following terminology is used:

dimension
  A measured property, for example length, pressure, temperature or
  time.

unit system
  A system of units for a given dimension, for example the SI units
  for the length dimension are metre and its derived units.  For the
  pressure, temperature and time dimensions, pascal, celsius and
  second are used respectively, along with any derived units.
  Multiple systems may be provided for a given dimension, such as
  Imperial length units, bar or Torr for pressure or Fahrenheit for
  temperature.  Different unit systems for the same dimension will
  typically be inter-convertible, but this is not a requirement.

unit
  A unit of measure within a given unit system, for example cm, µm and
  nm are all scaled units derived from m.

base unit
  The primary unit for a given unit system; all other units are scaled
  relative to this unit.  Automatic conversion between unit systems is
  defined in conversion of base units.  For example, m is the base
  unit for the SI length unit system.

quantity
  A measured value with an associated unit.  For example, 3.5 mm.

.. _tutorial_model_units:

Model units
^^^^^^^^^^^

The metadata interfaces make use of these unit types, which are based upon

- Unit type enumerations
- The :ome_xml_api:`Quantity class
  <classome_1_1xml_1_1model_1_1primitives_1_1Quantity.html>`

The :cpp:class:`Quantity` class is the user-visible part of the units
support.  It trades compile-time correctness for run-time flexibility.
It is templated, specialized for a given unit type enumeration, for
example ``Quantity<UnitLength>`` for length quantities.  It may
represent any valid unit from the enumeration.

A :cpp:class:`Quantity` is constructed using a numerical value and a
unit enumeration value.  Basic arithmetic operations such as
assignment, multiplication, subtraction, division and multiplication
are supported.  Note however that complex arithmetic with different
unit types is not supported; if this is required, please use the basic
units directly (see below).  Also note that multiplication and
division are permitted only with scalars, and addition and subtraction
require operands to be of the same type; the unit of the left-hand
operand will be preserved, and the value of the right-hand operand
will be implicitly converted to match.

Unit conversion is performed by using the free :cpp:func:`convert`
function, which requires a quantity and destination unit.  If
conversion is not supported, an exception will be thrown.  The
conversion operations are all performed in terms of the basic
quantities, described below.

The following example demonstrates these concepts:

.. literalinclude:: examples/units.cpp
   :language: cpp
   :start-after: model-example-start
   :end-before: model-example-end

Model units are also used in the following examples such as the
:ref:`tutorial_metadatastore`.

The model units are implemented using a lower-level units interface,
which we term "basic units".

.. _tutorial_basic_units:

Basic units
^^^^^^^^^^^

Unlike the model units, which provide run-time checking, the basic
units enforce correctness during compilation.  Basic units are
provided by a static compile-time type-safe unit system based upon
Boost.Units.  All the data types provided are simply typedefs or
specializations of the Boost.Units library unit types.  See the
`Boost.Units documentation
<http://www.boost.org/doc/libs/1_61_0/doc/html/boost_units/Units.html>`__
for further information.

As a user of the library, quantity types are the primary type which
you will use.  The other types are implementation details which will
not be needed unless you have a need to add additional custom units to
the existing set of registered units.  However, a comprehensive set of
SI, Imperial and other customary units are provided, so most of the
conceivable usage should be fully catered for.  A full list of
quantity types is listed in the :ome_common_api:`units namespace
<namespaceome_1_1common_1_1units.html>`.  Should you wish to add your
own quantities, units, unit systems and even entirely new dimensions,
this is all possible using Boost.Units.

To create a quantity, use the :cpp:func:`quantity::from_value` method,
which will return a :cpp:class:`quantity` of the desired type.  Since
the unit is encoded in the type name, the unit type is immutable and
correctness is enforced at compile time.  This differs from the model
units where the unit type is passed to the constructor.  Once
constructed, the quantity may be used in a similar manner to the model
quantities: a quantity is assignable to any quantity of the same type,
and arithmetic operations with scalar quantities or other units are
permitted.  Note that a quantity is not assignable to other quantities
in the same unit system which are not of exactly the same type;
implicit unit conversions are not allowed, even between scaled units
in the same unit system.  Also note that multiplication and division
with quantities may change the unit type, such as squaring when
multiplying the same unit type, and that adding and subtracting scalar
values is not permitted since there is no associated unit.

Explicit conversion is as simple as instantiating a quantity type with
a different quantity as the construction parameter.  If the conversion
is not permitted, this will result in a compilation failure, enforcing
correctness.

The following example demonstrates these concepts:

.. literalinclude:: examples/units.cpp
   :language: cpp
   :start-after: basic-example-start
   :end-before: basic-example-end

As a general rule, the user-facing API will not use basic units, but
they are available should you wish to make use of strict unit type
checking and unit type conversion in your own code.  They are also
potentially more performant when performing conversions since there is
much greater scope for optimization.

.. _tutorial_metadata:

Metadata
--------

OME-Files supports several different classes of metadata, from very
basic information about the image dimensions and pixel type to
detailed information about the acquisition hardware and experimental
parameters.  From simplest to most complex, these are:

Core metadata
  Basic information describing an individual 5D image (series),
  including dimension sizes, dimension order and pixel type

Original metadata
  Key-value pairs describing metadata from the original file format
  for the image.  Two forms exist: global metadata for an entire
  dataset (image collection) and series metadata for an individual 5D
  image

Metadata store
  A container for all image metadata providing interfaces to get and
  set individual metadata values.  This is a superset of the core and
  original metadata content (it can represent all values contained
  within the core and original metadata).  It is an alternative
  representation of the OME-XML data model objects, and is used by the
  OME-Files reader and writer interfaces.

OME-XML data model objects
  The abstract OME-XML data model is realized as a collection of
  *model objects*.  Classes are generated from the elements of the
  OME-XML data model schema, and a tree of the model objects acts as a
  representation of the OME data model which may be modified and
  manipulated.  The model objects may be created from an OME-XML text
  document, and vice versa.

For the simplest cases of reading and writing image data, the core
metadata interface will likely be sufficient.  If specific individual
parameters from the original file format are needed, then original
metadata may also be useful.  For more advanced processing and
rendering, the metadata store should be the next source of
information, for example to get information about the image scale,
stage position, instrument setup including light sources, light paths,
detectors etc., and access to plate/well information, regions of
interest etc.  Direct access to the OME-XML data model objects is an
alternative to the metadata store, but is more difficult to use;
certain modifications to the data model may only be made via direct
access to the model objects, otherwise the higher-level metadata store
interface should be preferred.

The header file :ome_files_api:`ome/files/MetadataTools.h
<MetadataTools_8h_source.html>` provides several convenience functions
to work with and manipulate the various forms of metadata, including
conversion of Core metadata to and from a metadata store.

.. _tutorial_core_metadata:

Core metadata
^^^^^^^^^^^^^

Core metadata is accessible through the getter methods in the
:cpp:class:`FormatReader` interface.  These operate on the *current*
series, set using the :cpp:func:`setSeries` method.  The
:cpp:class:`CoreMetadata` objects are also accessible directly using
the :cpp:class:`getCoreMetadataList` method.  The
:cpp:class:`FormatReader` interface should be preferred; the objects
themselves are more of an implementation detail at present.

.. literalinclude:: examples/metadata-formatreader.cpp
   :language: cpp
   :start-after: read-example-start
   :end-before: read-example-end

If implementing a reader, it is fairly typical to set the basic image
metadata in :cpp:class:`CoreMetadata` objects, and then use the
:cpp:func:`fillMetadata` function in
:ome_files_api:`ome/files/MetadataTools.h
<MetadataTools_8h_source.html>` to fill the reader's metadata store
with this information, before filling the metadata store with
additional (non-core) metadata as required.  When writing an image, a
metadata store is required in order to provide the writer with all the
metadata needed to write an image.  If the metadata store was not
already obtained from a reader, :cpp:func:`fillMetadata` may also be
used in this situation to create a suitable metadata store:

.. literalinclude:: examples/metadata-formatwriter.cpp
   :language: cpp
   :start-after: create-metadata-start
   :end-before: create-metadata-end

Full example source: :download:`metadata-formatreader.cpp
<examples/metadata-formatreader.cpp>`,
:download:`metadata-formatreader.cpp
<examples/metadata-formatwriter.cpp>`

.. seealso::

  - :ome_files_api:`CoreMetadata <classome_1_1files_1_1CoreMetadata.html>`
  - :ome_files_api:`FormatReader <classome_1_1files_1_1FormatReader.html>`

.. _tutorial_original_metadata:

Original metadata
^^^^^^^^^^^^^^^^^

Original metadata is stored in two forms: in a
:cpp:class:`MetadataMap` which is accessible through the
:cpp:class:`FormatReader` interface, which offers access to individual
keys and the whole map for both global and series metadata.  It is
also accessible using the metadata store; original metadata is stored
as an :cpp:class:`XMLAnnotation`.  The following example demonstrates
access to the global and series metadata using the
:cpp:class:`FormatReader` interface to get access to the maps:

.. literalinclude:: examples/metadata-formatreader.cpp
   :language: cpp
   :start-after: original-example-start
   :end-before: original-example-end

It would also be possible to use :cpp:func:`getMetadataValue` and
:cpp:func:`getSeriesMetadataValue` to obtain values for individual
keys.  Note that the :cpp:class:`MetadataMap` values can be scalar
values or lists of scalar values; call the :cpp:func:`flatten` method
to split the lists into separate key-value pairs with a numbered
suffix.

Full example source: :download:`metadata-formatreader.cpp <examples/metadata-formatreader.cpp>`

.. seealso::

  - :ome_files_api:`MetadataMap <classome_1_1files_1_1MetadataMap.html>`
  - :ome_files_api:`FormatReader <classome_1_1files_1_1FormatReader.html>`
  - :ome_xml_api:`OriginalMetadataAnnotation <classome_1_1xml_1_1model_1_1OriginalMetadataAnnotation.html>`

.. _tutorial_metadatastore:

Metadata store
^^^^^^^^^^^^^^

Access to metadata is provided via the :cpp:class:`MetadataStore` and
:cpp:class:`MetadataRetrieve` interfaces.  These provide setters and
getters, respectively, to store and retrieve metadata to and from an
underlying abstract metadata store.  The primary store is the
:cpp:class:`OMEXMLMetadata` which stores the metadata in OME-XML data
model objects (see below), and implements both interfaces.  However,
other storage classes are available, and may be used to filter the
stored metadata, combine different stores, or do nothing at all.
Additional storage backends could also be implemented, for example to
allow metadata retrieval from a relational database, or JSON/YAML.

When using :cpp:class:`OMEXMLMetadata` the convenience function
:cpp:func:`createOMEXMLMetadata` is the recommended method for
creating a new instance and then filling it with the content from an
OME-XML document.  This is overloaded to allow the OME-XML to be
obtained from various sources.  For example, from a file:

.. literalinclude:: examples/metadata-io.cpp
   :language: cpp
   :start-after: read-file-example-start
   :end-before: read-file-example-end

Alternatively from a DOM tree:

.. literalinclude:: examples/metadata-io.cpp
   :language: cpp
   :start-after: read-dom-example-start
   :end-before: read-dom-example-end

The convenience function :cpp:func:`getOMEXML` may be used to reverse
the process, i.e. obtain an OME-XML document from the store.  Note the
use of :cpp:func:`convert`.  Only the :cpp:class:`OMEXMLMetadata`
class can dump an OME-XML document, therefore if the source of the
data is another class implementing the :cpp:class:`MetadataRetrieve`
interface, the stored data will need to be copied into an
:cpp:class:`OMEXMLMetadata` instance first.

.. literalinclude:: examples/metadata-io.cpp
   :language: cpp
   :start-after: write-example-start
   :end-before: write-example-end

Conceptually, the metadata store contains lists of objects, accessed
by index (insertion order).  In the example below,
:cpp:func:`getImageCount` method is used to find the number of images.
This is then used to safely loop through each of the available images.
Each of the :cpp:func:`getPixelsSizeA` methods takes the image index
as its only argument.  Internally, this is used to find the
:cpp:class:`Image` model object for the specified index, and then call
the :cpp:func:`getSizeA` method on that object and return the result.
Since objects can contain other objects, some accessor methods require
the use of more than one index.  For example, an :cpp:class:`Image`
object can contain multiple :cpp:class:`Plane` objects.  Similar to
the above example, there is a :cpp:func:`getPlaneCount` method,
however since it is contained by an :cpp:class:`Image` it has an
additional image index argument to get the plane count for the
specified image.  Likewise its accessors such as
:cpp:func:`getPlaneTheZ` take two arguments, the image index and
the plane index.  Internally, these indices will be used to find the
:cpp:class:`Image`, then the :cpp:class:`Plane`, and then call
:cpp:func:`getTheZ`.  When using the :cpp:class:`MetadataRetrieve`
interface with an :cpp:class:`OMEXMLMetadata` store, the methods are
simply a shorthand for navigating through the tree of model objects.

.. literalinclude:: examples/metadata-io.cpp
   :language: cpp
   :start-after: query-example-start
   :end-before: query-example-end

The methods for storing data using the :cpp:class:`MetadataStore`
interface are similar.  The set methods use the same indices as the
get methods, with the value to set as an additional initial argument.
The following example demonstrates how to update dimension sizes for
images in the store:

.. literalinclude:: examples/metadata-io.cpp
   :language: cpp
   :start-after: update-example-start
   :end-before: update-example-end

When adding new objects to the store, as opposed to updating existing
ones, some additional considerations apply.  An new object is added to
the store if the object corresponding to an index does not exist and
the index is the current object count (i.e. one past the end of the
last valid index).  Note that for data model objects with a
:cpp:func:`setID` method, this method alone will trigger insertion and
must be called first, before any other methods which modify the
object.  The following example demonstrates the addition of a new
:cpp:class:`Image` to the store, plus contained :cpp:class:`Plane`
objects.

.. literalinclude:: examples/metadata-io.cpp
   :language: cpp
   :start-after: add-example-start
   :end-before: add-example-end

In addition to this basic metadata, it is possible to create and
modify extended metadata elements.  In the following example, we
describe the setup of the microscope during acquisition, including its
objective and detector parameters.  Only a few parameters are set
here; it is possible to completely describe the instrument
configuration, including the settings on a per-image and per-channel
basis if they vary during the course of acquisition.

.. literalinclude:: examples/metadata-formatwriter.cpp
   :language: cpp
   :start-after: extended-metadata-start
   :end-before: extended-metadata-end

If the existing data model elements and attributes are insufficient
for describing the complexity of your hardware or experimental setup,
it is possible to extend it with custom annotations.  These
annotations exist globally, but may be referenced by a model element
where needed, and may be referenced by multiple model elements if
required.  In the following example, we create and attach an
annotation to the ``Detector`` element, and then create and attach two
annotations to the first ``Image`` element.

.. literalinclude:: examples/metadata-formatwriter.cpp
   :language: cpp
   :start-after: annotations-start
   :end-before: annotations-end

Full example source: :download:`metadata-io.cpp
<examples/metadata-io.cpp>` and :download:`metadata-formatwriter.cpp
<examples/metadata-formatwriter.cpp>`

.. seealso::

  - :ome_xml_api:`Metadata classes <namespaceome_1_1xml_1_1meta.html>`
  - :ome_files_api:`createID <namespaceome_1_1files.html#ab7922c8cadf5f821ee7059ccb5f406f0>`
  - :ome_files_api:`createOMEXMLMetadata <namespaceome_1_1files.html#ae61f12958973765e8328348874a85731>`
  - :ome_files_api:`getOMEXML <namespaceome_1_1files.html#a32e5424991ce09b857ddc0d5be37c4f1>`


.. _tutorial_model:

OME-XML data model objects
^^^^^^^^^^^^^^^^^^^^^^^^^^

The data model objects are not typically used directly, but are
created, modified and queried using the :cpp:class:`Metadata`
interfaces (above), so in practice these examples should not be
needed.

To create a tree of OME-XML data model objects from OME-XML text:

.. literalinclude:: examples/model-io.cpp
   :language: cpp
   :start-after: read-example-start
   :end-before: read-example-end

In this example, the OME-XML text is read from a file into a DOM tree.
This could have been read directly from a string or stream if the
source was not a file.  The DOM tree is then processed using the
:cpp:class:`OME` root object's :cpp:func:`update` method, which uses
the data from the DOM tree elements to create a tree of corresponding
model objects contained by the root object.

To reverse the process, taking a tree of OME-XML model objects and
converting them back of OME-XML text:

.. literalinclude:: examples/model-io.cpp
   :language: cpp
   :start-after: write-example-start
   :end-before: write-example-end

Here, the :cpp:class:`OME` root object's :cpp:func:`asXMLElement`
method is used to copy the data from the OME root object and its
children into an XML DOM tree.  The DOM tree is then converted to text
for output.

As shown previously for the :cpp:class:`MetadataStore` API, it is also
possible to create and modify extended metadata elements using the
model objects directly.  The following example demonstrates the setup
of the microscope during acquisition, including its objective and
detector parameters, to achieve the same effect as in the example
above.

.. literalinclude:: examples/metadata-formatwriter2.cpp
   :language: cpp
   :start-after: extended-metadata-start
   :end-before: extended-metadata-end

Creating annotations and linking them to model objects is also
possible using model objects directly:

.. literalinclude:: examples/metadata-formatwriter2.cpp
   :language: cpp
   :start-after: annotations-start
   :end-before: annotations-end

Full example source: :download:`model-io.cpp <examples/model-io.cpp>`
and :download:`metadata-formatwriter2.cpp
<examples/metadata-formatwriter2.cpp>`

.. seealso::

  - :ome_xml_api:`OME model classes <namespaceome_1_1xml_1_1model.html>`
  - :ome_xml_api:`OME <classome_1_1xml_1_1model_1_1OME.html>`


.. _tutorial_pixeldata:

Pixel data
----------

The Bio-Formats Java implementation stores and passes pixel values in
a raw :cpp:type:`byte` array.  Due to limitations with C++ array
passing, this was not possible for the OME-Files C++ implementation.
While a vector or other container could have been used, several
problems remain.  The type and endianness of the data in the raw bytes
is not known, and the dimension ordering and dimension extents are
also unknown, which imposes a significant burden on the programmer to
correctly process the data.  The C++ implementation provides two types
to solve these problems.

The :cpp:class:`PixelBuffer` class is a container of pixel data.  It
is a template class, templated on the pixel type in use.  The class
contains the order of the dimensions, and the size of each dimension,
making it possible to process pixel data without need for
externally-provided metadata to describe its structure.  This class
may be used to contain and process pixel data of a specific pixel
type.  Internally, the pixel data is contained within a
:cpp:class:`boost::multi_array` as a 9D hyper-volume, though its usage
in this release of OME-Files is limited to 5D.  The class can either
contain its own memory allocation for pixel data, or it can reference
memory allocated or mapped externally, allowing use with memory-mapped
data, for example.

In many situations, it is desirable to work with arbitrary pixel
types, or at least the set of pixel types defined in the OME data
model in its :cpp:class:`PixelType` enumeration.  The
:cpp:class:`VariantPixelBuffer` fulfills this need, using
:cpp:class:`ome::compat::variant` to allow it to contain a
:cpp:class:`PixelBuffer` specialized for any of the pixel types in the
OME data model.  This is used to allow transfer and processing of any
supported pixel type, for example by the :cpp:class:`FormatReader`
class' :cpp:func:`getLookupTable` and :cpp:func:`openBytes` methods,
and the corresponding :cpp:class:`FormatWriter` class'
:cpp:func:`setLookupTable` and :cpp:func:`saveBytes` methods.

An additional problem with supporting many different pixel types is
that each operation upon the pixel data, for example for display or
analysis, may require implementing separately for each pixel type.
This imposes a significant testing and maintenance burden.
:cpp:class:`VariantPixelBuffer` solves this problem through use of
:cpp:func:`ome::compat::visit` and static visitor classes, which allow
algorithms to be defined in a template and compiled for each pixel
type.  They also allow algorithms to be specialized for different
classes of pixel type, for example signed vs. unsigned, integer
vs. floating point, or simple vs. complex, or special-cased per type
e.g. for bitmasks.  When :cpp:func:`ome::compat::visit` is called with
a specified algorithm and :cpp:class:`VariantPixelBuffer` object, it
will select the matching algorithm for the pixel type contained within
the buffer, and then invoke it on the buffer.  This permits the
programmer to support arbitrary pixel types without creating a
maintenance nightmare, and without unnecessary code duplication.

The 9D pixel buffer makes a distinction between the logical dimension
order (used by the API) and the storage order (the layout of the pixel
data in memory).  The logical order is defined by the values in the
:ome_files_api:`Dimensions
<namespaceome_1_1files.html#ad9ebb405a4815c189fa788325f68a91a>`
enum.  The storage order is specified by the programmer when creating
a pixel buffer.

The following example shows creation of a pixel buffer with a defined
size, and :ome_files_api:`default storage order
<classome_1_1files_1_1PixelBufferBase.html#a419ad49f2ea90937a57b81a74b56380b>`:

.. literalinclude:: examples/pixeldata.cpp
   :language: cpp
   :start-after: create-example-start
   :end-before: create-example-end

The storage order may be set explicitly.  The order may be created by
hand, or with a :ome_files_api:`helper function
<classome_1_1files_1_1PixelBufferBase.html#ac7e922610bf561f311d13c3d7fcaeb69>`.
While the helper function is limited to supporting the ordering
defined by the data model, specifying the order by hand allows
additional flexibility.  Manual ordering may be used to allow the
indexing for individual dimensions to run backward rather than
forward, which is useful if the Y-axis requires inverting, for
example.  The following example shows creation of two pixel buffers with
defined storage order using the helper function:

.. literalinclude:: examples/pixeldata.cpp
   :language: cpp
   :start-after: create-ordered-example-start
   :end-before: create-ordered-example-end

Note that the logical order of the dimension extents is unchanged.

Sometimes it may be necessary to change the storage order of data, for
example to give it the appropriate structure to pass to another
library with specific ordering requirements.  This can be done by a
simple assignment between two buffers having a different storage
order; the dimension extents must be of the same size for the buffers
to be compatible.  The following example demonstrates conversion of
planar data to contiguous:

.. literalinclude:: examples/pixeldata.cpp
   :language: cpp
   :start-after: reorder-example-start
   :end-before: reorder-example-end

In-place conversion is not yet supported.

In practice, it is unlikely that you will need to create any
:cpp:class:`PixelBuffer` objects directly.  The
:cpp:class:`FormatReader` and :cpp:class:`FormatWriter` interfaces use
:cpp:class:`VariantPixelBuffer` objects, and in the case of the reader
interface the :cpp:func:`getLookupTable` and :cpp:func:`openBytes`
methods can be passed a default-constructed
:cpp:class:`VariantPixelBuffer` and it will be set up automatically,
changing the image dimensions, dimension order and pixel type to match
the data being fetched, if the size, order and type do not match.  For
example, to read all pixel data in an image using
:cpp:func:`openBytes`:

.. literalinclude:: examples/metadata-formatreader.cpp
   :language: cpp
   :start-after: pixel-example-start
   :end-before: pixel-example-end

To perform the reverse process, writing pixel data with
:cpp:func:`saveBytes`:

.. literalinclude:: examples/metadata-formatwriter.cpp
   :language: cpp
   :start-after: pixel-example-start
   :end-before: pixel-example-end

Both buffer classes provide access to the pixel data so that it may be
accessed, manipulated and passed elsewhere.  The
:cpp:class:`PixelBuffer` class provides an :cpp:class:`at` method.
This allows access to individual pixel values using a 9D coordinate:

.. literalinclude:: examples/pixeldata.cpp
   :language: cpp
   :start-after: at-example-start
   :end-before: at-example-end

Conceptually, this is the same as using an index for a normal 1D
array, but extended to use an array of nine indices for each of the
nine dimensions, in the logical storage order.  The
:cpp:class:`VariantPixelBuffer` does not provide an :cpp:class:`at`
method for efficiency reasons.  Instead, visitors should be used for
the processing of bulk pixel data.  For example, this is one way the
minimum and maximum pixel values could be obtained:

.. literalinclude:: examples/pixeldata.cpp
   :language: cpp
   :start-after: visitor-example-start
   :end-before: visitor-example-end

This example demonstrates several features:

- The visitor operators can return values to the caller (for more
  complex algorithms, the visitor class could use member variables and
  additional methods)
- The operator is expanded once for each pixel type
- The operators can be special-cased for individual pixel types; here
  we use the `SFINAE rule
  <http://en.cppreference.com/w/cpp/language/sfinae>`_ to implement a
  specialization for an entire category of pixel types (complex
  numbers), but standard function overloading and templates will also
  work for more common cases
- Pixel data can be assigned to the buffer with a single
  :cpp:func:`assign` call.

The OME-Files source uses pixel buffer visitors for several purposes,
for example to load pixel data into OpenGL textures, which
automatically handles pixel format conversion and repacking of pixel
data as needed.

While the pixel buffers may appear complex, they do permit the OME
Files library to support all pixel types with relative ease, and it
will allow your applications to also handle multiple pixel types by
writing your own visitors.  Assignment of one buffer to another will
also repack the pixel data if they use different storage ordering
(i.e. the logical ordering is used for the copy), which can be useful
if you need the pixel data in a defined ordering.

If all you want is access to the raw data, as in the Java API, you are
not required to use the above features.  Simply use the
:cpp:func:`data` method on the buffer to get a pointer to the raw
data.  Note that you will need to multiply the buffer size obtained
with :cpp:func:`num_elements` by the size of the pixel type (use
:cpp:func:`bytesPerPixel` or ``sizeof`` on the buffer
:cpp:type:`value_type`).

Alternatively, it is also possible to access the underlying
:cpp:class:`boost::multi_array` using the :cpp:func:`array` method, if
you need access to functionality not wrapped by
:cpp:class:`PixelBuffer`.

Full example source: :download:`pixeldata.cpp <examples/pixeldata.cpp>`

.. seealso::

  - :ome_xml_api:`PixelType <classome_1_1xml_1_1model_1_1enums_1_1PixelType.html>`
  - :ome_files_api:`PixelBuffer <classome_1_1files_1_1PixelBuffer.html>`
  - :ome_files_api:`VariantPixelBuffer <classome_1_1files_1_1VariantPixelBuffer.html>`
  - :ome_files_api:`FormatReader::getLookupTable <classome_1_1files_1_1FormatReader.html#a9b69e3612f0ad4c945d1c0f111242cc2>`
  - :ome_files_api:`FormatReader::openBytes <classome_1_1files_1_1FormatReader.html#a5bfa86b4b68b03b63d76bb050cbe7101>`
  - :ome_files_api:`FormatWriter::setLookupTable <classome_1_1files_1_1FormatWriter.html#a00ae3dc46c205e64f782c7b6f47bd5ab>`
  - :ome_files_api:`FormatWriter::saveBytes <classome_1_1files_1_1FormatWriter.html#ad1e8b427214f7cfd19ce2251d38e24f5>`

Reading images
--------------

Image reading is performed using the :cpp:class:`FormatReader`
interface.  This is an abstract reader interface implemented by
file-format-specific reader classes.  Examples of readers include
:cpp:class:`TIFFReader`, which implements reading of Baseline TIFF
(optionally with additional ImageJ metadata), and
:cpp:class:`OMETIFFReader` which implements reading of OME-TIFF (TIFF
with OME-XML metadata).

Using a reader involves these steps:

#. Create a reader instance.
#. Set options to control reader behavior.
#. Call :cpp:func:`setId` to specify the image file to read.
#. Retrieve desired metadata and pixel data.
#. Close the reader.

These steps are illustrated in this example:

.. literalinclude:: examples/metadata-formatreader.cpp
   :language: cpp
   :start-after: reader-example-start
   :end-before: reader-example-end

Here we create a reader to read TIFF files, set two options (metadata
filtering and file grouping), and then call :cpp:func:`setId`.  At
this point the reader has been set up and initialized, and we can then
read metadata and pixel data, which we covered in the preceding
sections.  You might like to combine this example with the
:cpp:class:`MinMaxVisitor` example to make it display the minimum and
maximum values for each plane in an image; if you try running the
example with TIFF images of different pixel types, it will
transparently adapt to any supported pixel type.

.. note::

  Reader option-setting methods may only be called *before*
  :cpp:func:`setId`.  Reader state changing and querying methods such
  as :cpp:func:`setSeries` and :cpp:func:`getSeries`, metadata
  retrieval and pixel data retrieval methods may only be called
  *after* :cpp:func:`setId`.  If these constraints are violated, a
  :cpp:class:`FormatException` will be thrown.

Full example source: :download:`metadata-formatreader.cpp <examples/metadata-formatreader.cpp>`

.. seealso::

  - :ome_files_api:`FormatReader <classome_1_1files_1_1FormatReader.html>`
  - :ome_files_api:`TIFFReader <classome_1_1files_1_1in_1_1TIFFReader.html>`
  - :ome_files_api:`OMETIFFReader <classome_1_1files_1_1in_1_1OMETIFFReader.html>`

.. _tutorial_writing_images:

Writing images
--------------

Image writing is performed using the :cpp:class:`FormatWriter`
interface.  This is an abstract writer interface implemented by
file-format-specific writer classes.  Examples of writers include
:cpp:class:`MinimalTIFFWriter`, which implements writing of Baseline
TIFF and :cpp:class:`OMETIFFWriter` which implements writing of
OME-TIFF (TIFF with OME-XML metadata).

Using a writer involves these steps:

#. Create a writer instance.
#. Set metadata store to use.
#. Set options to control writer behavior.
#. Call :cpp:func:`setId` to specify the image file to write.
#. Store pixel data for each plane of each image in the specified
   dimension order.
#. Close the writer.

These steps are illustrated in this example:

.. literalinclude:: examples/metadata-formatwriter.cpp
   :language: cpp
   :start-after: writer-example-start
   :end-before: writer-example-end

Here we create a writer to write OME-TIFF files, set the metadata
store using metadata we create, then set a writer option (sample
interleaving), and then call :cpp:func:`setId`.  At this point the
writer has been set up and initialized, and we can then write the
pixel data, which we covered in the preceding sections.  Finally we
call :cpp:func:`close` to flush all data.

.. note::

  Metadata store setting and writer option-setting methods may only be
  called *before* :cpp:func:`setId`.  Writer state changing and
  querying methods such as :cpp:func:`setSeries` and
  :cpp:func:`getSeries`, and pixel data storage methods may only be
  called *after* :cpp:func:`setId`.  If these constraints are
  violated, a :cpp:class:`FormatException` will be thrown.

.. note::

  :cpp:func:`close` should be called explicitly to catch any errors.
  While this will be called by the destructor, the destructor can't
  throw exceptions and any errors will be silently ignored.

Full example source: :download:`metadata-formatwriter.cpp <examples/metadata-formatwriter.cpp>`

.. seealso::

  - :ome_files_api:`FormatWriter <classome_1_1files_1_1FormatWriter.html>`
  - :ome_files_api:`TIFFWriter <classome_1_1files_1_1out_1_1MinimalTIFFWriter.html>`
  - :ome_files_api:`OMETIFFWriter <classome_1_1files_1_1out_1_1OMETIFFWriter.html>`

Writing sub-resolution images
-----------------------------

Very large images may be accompanied by reduced-resolution copies of
the full resolution image.  These are also known as image "pyramids",
with the full-resolution image being reduced in size typically by
successive power of two reductions.  For example, if the full
resolution image measured 65536 × 65536 pixels, the reductions might
be be 32768 × 32768, 16384 × 16384, 8192 × 8192 and so on.  While
power of two reductions are conventional, power of three or reductions
of arbitrary size are possible.  The file format and the writer API
place no restrictions upon the possible sizes, except that each
reduction must be smaller in at least one of the X or Y dimensions.

.. note::

  Reductions in Z are not currently supported due to the OME data
  model being plane-based, with ``TiffData`` elements referencing
  planes, and the TIFF SubIFD field only permitting reduction in size
  of a single plane.  This limitation may be lifted with future model
  and file format changes.

Writing is essentially the same as the :ref:`tutorial_writing_images`
example, above, with a few additional steps.  The first step is to set
the sub-resolution levels for each series which requires them, by
adding them to the metadata store:

.. literalinclude:: examples/subresolution.cpp
   :language: cpp
   :start-after: create-metadata-start
   :end-before: create-metadata-end

In the above example, we compute the list of resolution levels
automatically.  The :cpp:func:`addResolutions` helper function adds
these to the metadata store as a custom annotation linked to the
specified image series.  If you need to, you can remove them with the
corresponding :cpp:func:`removeResolutions` function, or retrieve it
with the :cpp:func:`getResolutions` function.  There are additional
functions to add, remove and get all resolution levels for all series
at once.

.. note::

  The resolution annotations will be removed from the metadata store
  prior to generating the OME-XML to be embedded in the OME-TIFF file
  being written.  This is because these annotations are only used to
  provide the needed resolution information to the writer, and are not
  needed for reading since the resolution information is stored
  directly in the TIFF format as SubIFD fields.

Next, we will add some writer options:

.. literalinclude:: examples/subresolution.cpp
   :language: cpp
   :start-after: writer-options-start
   :end-before: writer-options-end

These options include interleaving (so that the RGB samples are stored
together rather than as separate planes), tiling (to improve random
access to big image planes), and compression (to reduce the image size
of such a large image).  These are all optional, but will generally
improve efficiency in both file size and read time.

Lastly, we can call :cpp:func:`setId` to initialise the writer with
all the above options, and then write the pixel data:

.. literalinclude:: examples/subresolution.cpp
   :language: cpp
   :start-after: pixel-data-start
   :end-before: pixel-data-end

In the previous example, we used the methods
:cpp:func:`getSeriesCount` and :cpp:func:`setSeries` to find out the
total number of images to write, and to switch between them in
ascending order to write out the pixel data associated with each
image.  In this example, we also make use of
:cpp:func:`getResolutionCount` and :cpp:func:`setResolution` to
additionally write out the reduced-resolution copies of each image.
As for the use of :cpp:func:`setSeries` in the previous example, the
resolution levels also require writing in strictly ascending order.

For the purposes of this example, the pixel data written here is a
fractal from the Mandelbrot or Julia set, since they can be scaled
infinitely and rendered as individual tiles.  In this example, we use
16× multisampling to smooth the rendered image and also use multiple
threads to generate and write the tiles concurrently, which may be of
interest if you wish to write tiled images in parallel.  The first
image is the Mandelbrot set, the second is from the Julia set:

.. image:: /images/mandelbrot.png

.. image:: /images/julia.png

As the image size is progressively reduced, there will be
correspondingly less detail in the image.  The lookup tables and
constants may be adjusted to alter the images.

Full example source: :download:`subresolution.cpp
<examples/subresolution.cpp>`, :download:`fractal.cpp
<examples/fractal.cpp>`, :download:`fractal.h <examples/fractal.h>`

.. seealso::

  - :ome_files_api:`FormatWriter <classome_1_1files_1_1FormatWriter.html>`
  - :ome_files_api:`OMETIFFWriter <classome_1_1files_1_1out_1_1OMETIFFWriter.html>`
