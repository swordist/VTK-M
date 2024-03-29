//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//
//  Copyright 2014 Sandia Corporation.
//  Copyright 2014 UT-Battelle, LLC.
//  Copyright 2014. Los Alamos National Security
//
//  Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
//  the U.S. Government retains certain rights in this software.
//
//  Under the terms of Contract DE-AC52-06NA25396 with Los Alamos National
//  Laboratory (LANL), the U.S. Government retains certain rights in
//  this software.
//============================================================================
#ifndef vtk_m_cont_ArrayHandle_h
#define vtk_m_cont_ArrayHandle_h

#include <vtkm/Types.h>

#include <vtkm/cont/Assert.h>
#include <vtkm/cont/ErrorControlBadValue.h>
#include <vtkm/cont/Storage.h>

#include <vtkm/cont/internal/ArrayHandleExecutionManager.h>
#include <vtkm/cont/internal/ArrayTransfer.h>
#include <vtkm/cont/internal/DeviceAdapterTag.h>

#include <boost/concept_check.hpp>
#include <boost/mpl/not.hpp>
#include <boost/smart_ptr/scoped_ptr.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>
#include <boost/type_traits/is_base_of.hpp>

#include <vector>

namespace vtkm {
namespace cont {

namespace internal {

/// Checks to see if the given type and storage can form a valid array handle
/// (some storage objects cannot support all types). This check is compatable
/// with the Boost meta-template programming library (MPL). It contains a
/// typedef named type that is either boost::mpl::true_ or boost::mpl::false_.
/// Both of these have a typedef named value with the respective boolean value.
///
template<typename T, typename StorageTag>
struct IsValidArrayHandle {
  typedef typename boost::mpl::not_<
    typename boost::is_base_of<
      vtkm::cont::internal::UndefinedStorage,
      vtkm::cont::internal::Storage<T,StorageTag>
      >::type
    >::type type;
};

} // namespace internal

/// \brief Manages an array-worth of data.
///
/// \c ArrayHandle manages as array of data that can be manipulated by VTKm
/// algorithms. The \c ArrayHandle may have up to two copies of the array, one
/// for the control environment and one for the execution environment, although
/// depending on the device and how the array is being used, the \c ArrayHandle
/// will only have one copy when possible.
///
/// An ArrayHandle can be constructed one of two ways. Its default construction
/// creates an empty, unallocated array that can later be allocated and filled
/// either by the user or a VTKm algorithm. The \c ArrayHandle can also be
/// constructed with iterators to a user's array. In this case the \c
/// ArrayHandle will keep a reference to this array but may drop it if the
/// array is reallocated.
///
/// \c ArrayHandle behaves like a shared smart pointer in that when it is copied
/// each copy holds a reference to the same array.  These copies are reference
/// counted so that when all copies of the \c ArrayHandle are destroyed, any
/// allocated memory is released.
///
///
template<
    typename T,
    typename StorageTag_ = VTKM_DEFAULT_STORAGE_TAG>
class ArrayHandle
{
private:
  typedef vtkm::cont::internal::Storage<T,StorageTag_> StorageType;
  typedef vtkm::cont::internal::ArrayHandleExecutionManagerBase<T,StorageTag_>
      ExecutionManagerType;
public:
  typedef T ValueType;
  typedef StorageTag_ StorageTag;
  typedef typename StorageType::PortalType PortalControl;
  typedef typename StorageType::PortalConstType PortalConstControl;
  template <typename DeviceAdapterTag>
  struct ExecutionTypes
  {
    typedef typename ExecutionManagerType
        ::template ExecutionTypes<DeviceAdapterTag>::Portal Portal;
    typedef typename ExecutionManagerType
        ::template ExecutionTypes<DeviceAdapterTag>::PortalConst PortalConst;
  };

  /// Constructs an empty ArrayHandle. Typically used for output or
  /// intermediate arrays that will be filled by a VTKm algorithm.
  ///
  VTKM_CONT_EXPORT ArrayHandle() : Internals(new InternalStruct)
  {
    this->Internals->UserPortalValid = false;
    this->Internals->ControlArrayValid = false;
    this->Internals->ExecutionArrayValid = false;
  }

  /// Constructs an ArrayHandle pointing to the data in the given array portal.
  ///
  VTKM_CONT_EXPORT ArrayHandle(const PortalConstControl& userData)
    : Internals(new InternalStruct)
  {
    this->Internals->UserPortal = userData;
    this->Internals->UserPortalValid = true;

    this->Internals->ControlArrayValid = false;
    this->Internals->ExecutionArrayValid = false;
  }

  /// Get the array portal of the control array.
  ///
  VTKM_CONT_EXPORT PortalControl GetPortalControl()
  {
    this->SyncControlArray();
    if (this->Internals->UserPortalValid)
    {
      throw vtkm::cont::ErrorControlBadValue(
        "ArrayHandle has a read-only control portal.");
    }
    else if (this->Internals->ControlArrayValid)
    {
      // If the user writes into the iterator we return, then the execution
      // array will become invalid. Play it safe and release the execution
      // resources. (Use the const version to preserve the execution array.)
      this->ReleaseResourcesExecution();
      return this->Internals->ControlArray.GetPortal();
    }
    else
    {
      throw vtkm::cont::ErrorControlBadValue("ArrayHandle contains no data.");
    }
  }

  /// Get the array portal of the control array.
  ///
  VTKM_CONT_EXPORT PortalConstControl GetPortalConstControl() const
  {
    this->SyncControlArray();
    if (this->Internals->UserPortalValid)
    {
      return this->Internals->UserPortal;
    }
    else if (this->Internals->ControlArrayValid)
    {
      return this->Internals->ControlArray.GetPortalConst();
    }
    else
    {
      throw vtkm::cont::ErrorControlBadValue("ArrayHandle contains no data.");
    }
  }

  /// Returns the number of entries in the array.
  ///
  VTKM_CONT_EXPORT vtkm::Id GetNumberOfValues() const
  {
    if (this->Internals->UserPortalValid)
    {
      return this->Internals->UserPortal.GetNumberOfValues();
    }
    else if (this->Internals->ControlArrayValid)
    {
      return this->Internals->ControlArray.GetNumberOfValues();
    }
    else if (this->Internals->ExecutionArrayValid)
    {
      return
        this->Internals->ExecutionArray->GetNumberOfValues();
    }
    else
    {
      return 0;
    }
  }

  /// \brief Reduces the size of the array without changing its values.
  ///
  /// This method allows you to resize the array without reallocating it. The
  /// number of entries in the array is changed to \c numberOfValues. The data
  /// in the array (from indices 0 to \c numberOfValues - 1) are the same, but
  /// \c numberOfValues must be equal or less than the preexisting size
  /// (returned from GetNumberOfValues). That is, this method can only be used
  /// to shorten the array, not lengthen.
  void Shrink(vtkm::Id numberOfValues)
  {
    vtkm::Id originalNumberOfValues = this->GetNumberOfValues();

    if (numberOfValues < originalNumberOfValues)
    {
      if (this->Internals->UserPortalValid)
      {
        throw vtkm::cont::ErrorControlBadValue(
          "ArrayHandle has a read-only control portal.");
      }
      if (this->Internals->ControlArrayValid)
      {
        this->Internals->ControlArray.Shrink(numberOfValues);
      }
      if (this->Internals->ExecutionArrayValid)
      {
        this->Internals->ExecutionArray->Shrink(numberOfValues);
      }
    }
    else if (numberOfValues == originalNumberOfValues)
    {
      // Nothing to do.
    }
    else // numberOfValues > originalNumberOfValues
    {
      throw vtkm::cont::ErrorControlBadValue(
        "ArrayHandle::Shrink cannot be used to grow array.");
    }

    VTKM_ASSERT_CONT(this->GetNumberOfValues() == numberOfValues);
  }

  /// Releases any resources being used in the execution environment (that are
  /// not being shared by the control environment).
  ///
  VTKM_CONT_EXPORT void ReleaseResourcesExecution()
  {
    if (this->Internals->ExecutionArrayValid)
    {
      this->Internals->ExecutionArray->ReleaseResources();
      this->Internals->ExecutionArrayValid = false;
    }
  }

  /// Releases all resources in both the control and execution environments.
  ///
  VTKM_CONT_EXPORT void ReleaseResources()
  {
    this->ReleaseResourcesExecution();

    // Forget about any user iterators.
    this->Internals->UserPortalValid = false;

    if (this->Internals->ControlArrayValid)
    {
      this->Internals->ControlArray.ReleaseResources();
      this->Internals->ControlArrayValid = false;
    }
  }

  /// Prepares this array to be used as an input to an operation in the
  /// execution environment. If necessary, copies data to the execution
  /// environment. Can throw an exception if this array does not yet contain
  /// any data. Returns a portal that can be used in code running in the
  /// execution environment.
  ///
  template<typename DeviceAdapterTag>
  VTKM_CONT_EXPORT
  typename ExecutionTypes<DeviceAdapterTag>::PortalConst
  PrepareForInput(DeviceAdapterTag) const
  {
    VTKM_IS_DEVICE_ADAPTER_TAG(DeviceAdapterTag);

    if (this->Internals->ExecutionArrayValid)
    {
      // Nothing to do, data already loaded.
    }
    else if (this->Internals->UserPortalValid)
    {
      VTKM_ASSERT_CONT(!this->Internals->ControlArrayValid);
      this->PrepareForDevice(DeviceAdapterTag());
      this->Internals->ExecutionArray->LoadDataForInput(
        this->Internals->UserPortal);
      this->Internals->ExecutionArrayValid = true;
    }
    else if (this->Internals->ControlArrayValid)
    {
      this->PrepareForDevice(DeviceAdapterTag());
      this->Internals->ExecutionArray->LoadDataForInput(
        this->Internals->ControlArray);
      this->Internals->ExecutionArrayValid = true;
    }
    else
    {
      throw vtkm::cont::ErrorControlBadValue(
        "ArrayHandle has no data when PrepareForInput called.");
    }
    return this->Internals->ExecutionArray->GetPortalConstExecution(
             DeviceAdapterTag());
  }

  /// Prepares (allocates) this array to be used as an output from an operation
  /// in the execution environment. The internal state of this class is set to
  /// have valid data in the execution array with the assumption that the array
  /// will be filled soon (i.e. before any other methods of this object are
  /// called). Returns a portal that can be used in code running in the
  /// execution environment.
  ///
  template<typename DeviceAdapterTag>
  VTKM_CONT_EXPORT
  typename ExecutionTypes<DeviceAdapterTag>::Portal
  PrepareForOutput(vtkm::Id numberOfValues, DeviceAdapterTag)
  {
    VTKM_IS_DEVICE_ADAPTER_TAG(DeviceAdapterTag);

    // Invalidate any control arrays.
    // Should the control array resource be released? Probably not a good
    // idea when shared with execution.
    this->Internals->UserPortalValid = false;
    this->Internals->ControlArrayValid = false;

    this->PrepareForDevice(DeviceAdapterTag());
    this->Internals->ExecutionArray->AllocateArrayForOutput(
      this->Internals->ControlArray, numberOfValues);

    // We are assuming that the calling code will fill the array using the
    // iterators we are returning, so go ahead and mark the execution array as
    // having valid data. (A previous version of this class had a separate call
    // to mark the array as filled, but that was onerous to call at the the
    // right time and rather pointless since it is basically always the case
    // that the array is going to be filled before anything else. In this
    // implementation the only access to the array is through the iterators
    // returned from this method, so you would have to work to invalidate this
    // assumption anyway.)
    this->Internals->ExecutionArrayValid = true;

    return this->Internals->ExecutionArray->GetPortalExecution(DeviceAdapterTag());
  }

  /// Prepares this array to be used in an in-place operation (both as input
  /// and output) in the execution environment. If necessary, copies data to
  /// the execution environment. Can throw an exception if this array does not
  /// yet contain any data. Returns a portal that can be used in code running
  /// in the execution environment.
  ///
  template<typename DeviceAdapterTag>
  VTKM_CONT_EXPORT
  typename ExecutionTypes<DeviceAdapterTag>::Portal
  PrepareForInPlace(DeviceAdapterTag)
  {
    VTKM_IS_DEVICE_ADAPTER_TAG(DeviceAdapterTag);

    if (this->Internals->UserPortalValid)
    {
      throw vtkm::cont::ErrorControlBadValue(
        "In place execution cannot be used with an ArrayHandle that has "
        "user arrays because this might write data back into user space "
        "unexpectedly.  Copy the data to a new array first.");
    }

    // This code is similar to PrepareForInput except that we have to give a
    // writable portal instead of the const portal to the execution array
    // manager so that the data can (potentially) be written to.
    if (this->Internals->ExecutionArrayValid)
    {
      // Nothing to do, data already loaded.
    }
    else if (this->Internals->ControlArrayValid)
    {
      this->PrepareForDevice(DeviceAdapterTag());
      this->Internals->ExecutionArray->LoadDataForInPlace(
        this->Internals->ControlArray);
      this->Internals->ExecutionArrayValid = true;
    }
    else
    {
      throw vtkm::cont::ErrorControlBadValue(
        "ArrayHandle has no data when PrepareForInput called.");
    }

    // Invalidate any control arrays since their data will become invalid when
    // the execution data is overwritten. Don't actually release the control
    // array. It may be shared as the execution array.
    this->Internals->ControlArrayValid = false;

    return this->Internals->ExecutionArray->GetPortalExecution(DeviceAdapterTag());
  }

// protected:
  /// Special constructor for subclass specializations that need to set the
  /// initial state of the control array. When this constructor is used, it
  /// is assumed that the control array is valid.
  ///
  ArrayHandle(const StorageType &storage)
    : Internals(new InternalStruct)
  {
    this->Internals->UserPortalValid = false;
    this->Internals->ControlArray = storage;
    this->Internals->ControlArrayValid = true;
    this->Internals->ExecutionArrayValid = false;
  }

  // private:
  struct InternalStruct
  {
    PortalConstControl UserPortal;
    bool UserPortalValid;

    StorageType ControlArray;
    bool ControlArrayValid;

    boost::scoped_ptr<
      vtkm::cont::internal::ArrayHandleExecutionManagerBase<
        ValueType,StorageTag> > ExecutionArray;
    bool ExecutionArrayValid;
  };

  ArrayHandle(boost::shared_ptr<InternalStruct> i)
    : Internals(i)
  { }

  /// Gets this array handle ready to interact with the given device. If the
  /// array handle has already interacted with this device, then this method
  /// does nothing. Although the internal state of this class can change, the
  /// method is declared const because logically the data does not.
  ///
  template<typename DeviceAdapterTag>
  VTKM_CONT_EXPORT
  void PrepareForDevice(DeviceAdapterTag) const
  {
    if (this->Internals->ExecutionArray != NULL)
    {
      if (this->Internals->ExecutionArray->IsDeviceAdapter(DeviceAdapterTag()))
      {
        // Already have manager for correct device adapter. Nothing to do.
        return;
      }
      else
      {
        // Have the wrong manager. Delete the old one and create a new one
        // of the right type. (BTW, it would be possible for the array handle
        // to hold references to execution arrays on multiple devices. However,
        // there is not a clear use case for that yet and it is unclear what
        // the behavior of "dirty" arrays should be, so it is not currently
        // implemented.)
        this->SyncControlArray();
        // Need to change some state that does not change the logical state from
        // an external point of view.
        InternalStruct *internals
            = const_cast<InternalStruct*>(this->Internals.get());
        internals->ExecutionArray.reset();
        internals->ExecutionArrayValid = false;
        }
      }

    VTKM_ASSERT_CONT(this->Internals->ExecutionArray == NULL);
    VTKM_ASSERT_CONT(this->Internals->ExecutionArrayValid == false);
    // Need to change some state that does not change the logical state from
    // an external point of view.
    InternalStruct *internals
        = const_cast<InternalStruct*>(this->Internals.get());
    internals->ExecutionArray.reset(
          new vtkm::cont::internal::ArrayHandleExecutionManager<
            T, StorageTag, DeviceAdapterTag>);
  }

  /// Synchronizes the control array with the execution array. If either the
  /// user array or control array is already valid, this method does nothing
  /// (because the data is already available in the control environment).
  /// Although the internal state of this class can change, the method is
  /// declared const because logically the data does not.
  ///
  VTKM_CONT_EXPORT void SyncControlArray() const
  {
    if (   !this->Internals->UserPortalValid
           && !this->Internals->ControlArrayValid)
    {
      // Need to change some state that does not change the logical state from
      // an external point of view.
      InternalStruct *internals
        = const_cast<InternalStruct*>(this->Internals.get());
      internals->ExecutionArray->RetrieveOutputData(internals->ControlArray);
      internals->ControlArrayValid = true;
    }
    else
    {
      // It should never be the case that both the user and control array are
      // valid.
      VTKM_ASSERT_CONT(!this->Internals->UserPortalValid
                       || !this->Internals->ControlArrayValid);
      // Nothing to do.
    }
  }

  boost::shared_ptr<InternalStruct> Internals;
};

/// A convenience function for creating an ArrayHandle from a standard C array.
/// Unless properly specialized, this only works with storage types that use an
/// array portal that accepts a pair of pointers to signify the beginning and
/// end of the array.
///
template<typename T, typename StorageTag>
VTKM_CONT_EXPORT
vtkm::cont::ArrayHandle<T, StorageTag>
make_ArrayHandle(const T *array,
                 vtkm::Id length,
                 StorageTag)
{
  typedef vtkm::cont::ArrayHandle<T, StorageTag> ArrayHandleType;
  typedef typename ArrayHandleType::PortalConstControl PortalType;
  return ArrayHandleType(PortalType(array, array+length));
}
template<typename T>
VTKM_CONT_EXPORT
vtkm::cont::ArrayHandle<T, VTKM_DEFAULT_STORAGE_TAG>
make_ArrayHandle(const T *array, vtkm::Id length)
{
  return make_ArrayHandle(array,
                          length,
                          VTKM_DEFAULT_STORAGE_TAG());
}

/// A convenience function for creating an ArrayHandle from an std::vector.
/// Unless properly specialized, this only works with storage types that use an
/// array portal that accepts a pair of pointers to signify the beginning and
/// end of the array.
///
template<typename T,
         typename Allocator,
         typename StorageTag>
VTKM_CONT_EXPORT
vtkm::cont::ArrayHandle<T, StorageTag>
make_ArrayHandle(const std::vector<T,Allocator> &array,
                 StorageTag)
{
  typedef vtkm::cont::ArrayHandle<T, StorageTag> ArrayHandleType;
  typedef typename ArrayHandleType::PortalConstControl PortalType;
  return ArrayHandleType(PortalType(&array.front(), &array.back() + 1));
}
template<typename T, typename Allocator>
VTKM_CONT_EXPORT
vtkm::cont::ArrayHandle<T, VTKM_DEFAULT_STORAGE_TAG>
make_ArrayHandle(const std::vector<T,Allocator> &array)
{
  return make_ArrayHandle(array, VTKM_DEFAULT_STORAGE_TAG());
}

}
}

#endif //vtk_m_cont_ArrayHandle_h
