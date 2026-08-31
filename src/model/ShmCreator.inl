/**
 * @file ShmCreator.inl
 * @brief 共享内存创建器实现
*/


template<typename T>
ShmCreator<T>::ShmCreator(const std::string& name, uint32_t total_size) 
    : m_total_size(total_size),
      m_shm_ptr(NULL),
      m_shm_fd(-1),
      m_is_owner(false),
      m_shm_name(name) {
    
}

template<typename T>
ShmCreator<T>::~ShmCreator() {
    Close();
}

template<typename T>
T* ShmCreator<T>::get_shm_ptr() const {
    return m_shm_ptr;
}

template<typename T>
bool ShmCreator<T>::create_shm(bool create) {
#if defined(__linux__)
    if (!create) {
        struct stat st;
        if (fstat(m_shm_fd, &st) != 0 || st.st_size == 0) {
            LOG_ERROR("ShmCreator: fstat failed, st.st_size = %ld", st.st_size);
            return false;
        }
        m_total_size = static_cast<uint32_t>(st.st_size);
    } else {
        if (ftruncate(m_shm_fd, m_total_size) != 0) {
            LOG_ERROR("ShmCreator: ftruncate failed, m_total_size = %d", m_total_size);
            return false;
        }
    }

    void* ptr = mmap(NULL, m_total_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_shm_fd, 0);
    if (ptr == MAP_FAILED) {
        LOG_ERROR("ShmCreator: mmap failed");
        m_shm_ptr = NULL;
        return false;
    }
    m_shm_ptr = static_cast<T*>(ptr);
    return true;
#else
    (void)create;
    return false;
#endif
}

template<typename T>
bool ShmCreator<T>::Open(bool create) {
#if defined(__linux__)
    m_is_owner = create;
    if (create) {
        m_shm_fd = shm_open(m_shm_name.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
        if (m_shm_fd >= 0) {
            return create_shm(true);
        } else {
            m_shm_fd = shm_open(m_shm_name.c_str(), O_RDWR, 0666);
            if (m_shm_fd >= 0) {
                return create_shm(false);
            } else {
                LOG_ERROR("ShmCreator: open failed, m_shm_fd = %d", m_shm_fd);
            }
        }
    } else {
        m_shm_fd = shm_open(m_shm_name.c_str(), O_RDWR, 0666);
        if (m_shm_fd >= 0) {
            return create_shm(false);
        } else {
            LOG_ERROR("ShmCreator: open failed, m_shm_fd = %d", m_shm_fd);
        }
    }
    return false;
#else
    (void)create;
    return false;
#endif
}

template<typename T>
void ShmCreator<T>::delete_shm() {
#if defined(__linux__)
    if (m_is_owner) {
        Close();
        shm_unlink(m_shm_name.c_str());
    }
#endif
}

template<typename T>
void ShmCreator<T>::Close() {
#if defined(__linux__)
    if (m_shm_ptr) {
        munmap(static_cast<void*>(m_shm_ptr), m_total_size);
        m_shm_ptr = NULL;
    }
    if (m_shm_fd >= 0) {
        ::close(m_shm_fd);
        m_shm_fd = -1;
    }
#else
    m_shm_ptr = NULL;
    m_shm_fd = -1;
#endif
    LOG_DEBUG("ShmCreator: close success, m_shm_name = %s", m_shm_name.c_str());
}

template<typename T>
bool ShmCreator<T>::valid() const {
    return static_cast<void*>(m_shm_ptr) && static_cast<void*>(m_shm_ptr) != MAP_FAILED;
}

template<typename T>
std::string ShmCreator<T>::get_shm_name() {
    return m_shm_name;
}
