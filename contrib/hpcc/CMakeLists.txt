if(${ENABLE_MPI})
  set(hpcc_mpi_source model/qbb-remote-channel.cc)
  set(hpcc_mpi_header model/qbb-remote-channel.h)
  set(hpcc_mpi_libs ${libmpi} MPI::MPI_CXX)
endif()

build_lib(
  LIBNAME hpcc
  SOURCE_FILES
    ${hpcc_mpi_source}
    helper/leaky-bucket-helper.cc
    helper/qbb-helper.cc
    helper/rdma-client-helper.cc
    model/broadcom-egress-queue.cc
    model/cn-header.cc
    model/custom-header.cc
    model/hpcc-ppp-header.cc
    model/int-header.cc
    model/leaky-bucket.cc
    model/pause-header.cc
    model/pint.cc
    model/qbb-channel.cc
    model/qbb-header.cc
    model/qbb-net-device.cc
    model/rdma-client.cc
    model/rdma-driver.cc
    model/rdma-hw.cc
    model/rdma-queue-pair.cc
    model/rdma-seq-ts-header.cc
    model/switch-mmu.cc
    model/switch-node.cc
  HEADER_FILES
    ${hpcc_mpi_header}
    helper/leaky-bucket-helper.h
    helper/qbb-helper.h
    helper/rdma-client-helper.h
    helper/sim-setting.h
    model/broadcom-egress-queue.h
    model/cn-header.h
    model/custom-header.h
    model/hpcc-ppp-header.h
    model/int-header.h
    model/leaky-bucket.h
    model/pause-header.h
    model/pint.h
    model/qbb-channel.h
    model/qbb-header.h
    model/qbb-net-device.h
    model/rdma-client.h
    model/rdma-driver.h
    model/rdma-hw.h
    model/rdma-queue-pair.h
    model/rdma-seq-ts-header.h
    model/switch-mmu.h
    model/switch-node.h
    model/trace-format.h
  TEST_SOURCES test/hpcc-test-suite.cc
  LIBRARIES_TO_LINK ${libpoint-to-point} ${libinternet} ${hpcc_mpi_libs}
)
