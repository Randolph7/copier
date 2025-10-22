#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include "copier.h"

namespace google {
    namespace protobuf {
        namespace io {
            class ArrayInputStreamCopier final : public ZeroCopyInputStream {
            public:
                // Create an InputStream that returns the bytes pointed to by "data".
                // "data" remains the property of the caller but must remain valid until
                // the stream is destroyed.  If a block_size is given, calls to Next()
                // will return data blocks no larger than the given size.  Otherwise, the
                // first call to Next() returns the entire array.  block_size is mainly
                // useful for testing; in production you would probably never want to set
                // it.
                ArrayInputStreamCopier(const void *data, int size, int block_size = -1,
                                       struct copier::smartInputDescriptorBuffer *descriptor = NULL);

                ArrayInputStreamCopier(const void *data, int size, int block_size = -1);

                ~ArrayInputStreamCopier() override = default;

                // `ArrayInputStream` is neither copiable nor assignable
                ArrayInputStreamCopier(const ArrayInputStreamCopier &) = delete;

                ArrayInputStreamCopier &operator=(const ArrayInputStreamCopier &) = delete;

                // implements ZeroCopyInputStream ----------------------------------
                bool Next(const void **data, int *size) override;

                void BackUp(int count) override;

                bool Skip(int count) override;

                int64_t ByteCount() const override;

            private:
                const uint8_t *const data_;  // The byte array.
                const int size_;           // Total size of the array.
                const int block_size_;     // How many bytes to return at a time.

                int position_;
                struct copier::smartInputDescriptorBuffer *descriptor;
                int last_returned_size_;  // How many bytes we returned last time Next()
                // was called (used for error checking only).
            };

        }
    }
}