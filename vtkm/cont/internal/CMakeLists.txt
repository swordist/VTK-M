##============================================================================
##  Copyright (c) Kitware, Inc.
##  All rights reserved.
##  See LICENSE.txt for details.
##  This software is distributed WITHOUT ANY WARRANTY; without even
##  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
##  PURPOSE.  See the above copyright notice for more information.
##
##  Copyright 2014 Sandia Corporation.
##  Copyright 2014 UT-Battelle, LLC.
##  Copyright 2014. Los Alamos National Security
##
##  Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
##  the U.S. Government retains certain rights in this software.
##
##  Under the terms of Contract DE-AC52-06NA25396 with Los Alamos National
##  Laboratory (LANL), the U.S. Government retains certain rights in
##  this software.
##============================================================================

set(headers
  ArrayHandleExecutionManager.h
  ArrayManagerExecution.h
  ArrayManagerExecutionSerial.h
  ArrayManagerExecutionShareWithControl.h
  ArrayPortalFromIterators.h
  ArrayPortalShrink.h
  ArrayTransfer.h
  DeviceAdapterAlgorithm.h
  DeviceAdapterAlgorithmGeneral.h
  DeviceAdapterAlgorithmSerial.h
  DeviceAdapterError.h
  DeviceAdapterTag.h
  DeviceAdapterTagSerial.h
  DynamicTransform.h
  IteratorFromArrayPortal.h
  PointCoordinatesBase.h
  SimplePolymorphicContainer.h
  StorageError.h
  )

vtkm_declare_headers(${headers})

# These source files are actually compiled in the parent directory.
# They are in a separate directory to highlight which objects are
# internal and which are part of the external interface.
#add_custom_target(vtkmContInternal ALL DEPENDS vtkmCont)

add_subdirectory(testing)
