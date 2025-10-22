#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include "copier.h"
#include "array_input_stream.h"

namespace google {
    namespace protobuf {
        namespace io {
            ArrayInputStreamCopier::ArrayInputStreamCopier(const void *data, int size, int block_size)
                    : data_(reinterpret_cast<const uint8_t *>(data)),
                      size_(size),
                      block_size_(block_size > 0 ? block_size : size),
                      position_(0),
                      last_returned_size_(0) {}

            ArrayInputStreamCopier::ArrayInputStreamCopier(const void *data, int size, int block_size,
                                                           struct copier::smartInputDescriptorBuffer *descriptor)
                    : data_(reinterpret_cast<const uint8_t *>(data)),
                      size_(size),
                      block_size_(block_size > 0 ? block_size : size),
                      position_(0),
                      last_returned_size_(0),
                      descriptor(descriptor) {}

            bool ArrayInputStreamCopier::Next(const void **data, int *size) {
                if (position_ < size_) {
                    last_returned_size_ = std::min(block_size_, size_ - position_);
                    copier::smartInputDescriptorBufferGet((void *) data_, descriptor, position_, last_returned_size_);
                    *data = data_ + position_;
                    __builtin_prefetch(*data, 0, 0);
                    *size = last_returned_size_;
                    position_ += last_returned_size_;
                    return true;
                } else {
                    // We're at the end of the array.
                    last_returned_size_ = 0;  // Don't let caller back up.
                    return false;
                }
            }

            void ArrayInputStreamCopier::BackUp(int count) {
//   ABSL_CHECK_GT(last_returned_size_, 0)
//       << "BackUp() can only be called after a successful Next().";
//   ABSL_CHECK_LE(count, last_returned_size_);
//   ABSL_CHECK_GE(count, 0);
                position_ -= count;
                last_returned_size_ = 0;  // Don't let caller back up further.
            }

            bool ArrayInputStreamCopier::Skip(int count) {
//   ABSL_CHECK_GE(count, 0);
                last_returned_size_ = 0;  // Don't let caller back up.
                if (count > size_ - position_) {
                    position_ = size_;
                    return false;
                } else {
                    position_ += count;
                    return true;
                }
            }

            int64_t ArrayInputStreamCopier::ByteCount() const { return position_; }
        }
    }
}