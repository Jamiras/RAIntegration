#ifndef RA_SERVICES_MOCK_FILESYSTEM_HH
#define RA_SERVICES_MOCK_FILESYSTEM_HH
#pragma once

#include "services\IFileSystem.hh"
#include "services\ServiceLocator.hh"
#include "services\impl\StringTextReader.hh"
#include "services\impl\StringTextWriter.hh"

#include <set>
#include <unordered_map>

namespace ra {
namespace services {
namespace mocks {

class MockFileSystem : public IFileSystem
{
public:
    MockFileSystem() noexcept 
        : m_Override(this)
    {
    }

    const std::wstring& BaseDirectory() const override { return m_sBaseDirectory; }
    void SetBaseDirectory(const std::wstring& sBaseDirectory)
    {
        m_sBaseDirectory = sBaseDirectory;
    }

    bool DirectoryExists(const std::wstring& sDirectory) const override
    {
        return m_vDirectories.find(sDirectory) != m_vDirectories.end();
    }

    bool CreateDirectory(const std::wstring& sDirectory) const override
    {
        m_vDirectories.insert(sDirectory);
        return true;
    }
    
    /// <summary>
    /// Mocks the contents of a file.
    /// </summary>
    /// <param name="sPath">The path of the file.</param>
    /// <param name="sContents">The contents of the file.</param>
    void MockFile(const std::wstring& sPath, const std::string& sContents)
    {
        m_mFileSizes.erase(sPath);
        m_mFileContents.insert_or_assign(sPath, sContents);
    }

    const std::string& GetFileContents(const std::wstring& sPath)
    {
        auto pIter = m_mFileContents.find(sPath);
        if (pIter != m_mFileContents.end())
            return pIter->second;

        static const std::string sEmpty;
        return sEmpty;
    }
    
    /// <summary>
    /// Mocks the size of the file.
    /// </summary>
    /// <param name="sPath">The path of the file.</param>
    /// <param name="nFileSize">The size of the file.</param>
    /// <remarls>Mocked size will be discarded if the file is updated (MockFile, CreateTextFile, or AppendTextFile is called for the file).</remarks>
    void MockFileSize(const std::wstring& sPath, int64_t nFileSize)
    {
        m_mFileSizes.insert_or_assign(sPath, nFileSize);
    }

    int64_t GetFileSize(const std::wstring& sPath) const override
    {
        auto pIter = m_mFileSizes.find(sPath);
        if (pIter != m_mFileSizes.end())
            return pIter->second;

        auto pIter2 = m_mFileContents.find(sPath);
        if (pIter2 != m_mFileContents.end())
            return pIter2->second.length();

        return -1;
    }

    bool DeleteFile(const std::wstring& sPath) const override
    {
        m_mFileSizes.erase(sPath);
        return m_mFileContents.erase(sPath) != 0;
    }

    bool MoveFile(const std::wstring& sOldPath, const std::wstring& sNewPath) const override
    {
        auto hNode = m_mFileContents.extract(sOldPath);
        if (hNode.empty())
            return false;

        hNode.key() = sNewPath;
        m_mFileContents.insert(std::move(hNode));

        auto hNode2 = m_mFileSizes.extract(sOldPath);
        if (!hNode2.empty())
        {
            hNode2.key() = sNewPath;
            m_mFileSizes.insert(std::move(hNode2));
        }

        return true;
    }

    std::unique_ptr<TextReader> OpenTextFile(const std::wstring& sPath) const override
    {
        auto pIter = m_mFileContents.find(sPath);
        if (pIter == m_mFileContents.end())
            return std::unique_ptr<TextReader>();

        auto pReader = std::make_unique<ra::services::impl::StringTextReader>(pIter->second);
        return std::unique_ptr<TextReader>(pReader.release());
    }

    std::unique_ptr<TextWriter> CreateTextFile(const std::wstring& sPath) const override
    {
        m_mFileSizes.erase(sPath);

        // insert_or_assign will replace any existing value
        auto iter = m_mFileContents.insert_or_assign(sPath, "");
        auto pWriter = std::make_unique<ra::services::impl::StringTextWriter>(iter.first->second);
        return std::unique_ptr<TextWriter>(pWriter.release());
    }

    std::unique_ptr<TextWriter> AppendTextFile(const std::wstring& sPath) const override
    {
        m_mFileSizes.erase(sPath);

        // insert will return a pointer to the new (or previously existing) value
        auto iter = m_mFileContents.insert({ sPath, "" });
        auto pWriter = std::make_unique<ra::services::impl::StringTextWriter>(iter.first->second);
        return std::unique_ptr<TextWriter>(pWriter.release());
    }

private:
    ra::services::ServiceLocator::ServiceOverride<ra::services::IFileSystem> m_Override;
    std::wstring m_sBaseDirectory = L".\\";
    mutable std::set<std::wstring> m_vDirectories;
    mutable std::unordered_map<std::wstring, std::string> m_mFileContents;
    mutable std::unordered_map<std::wstring, int64_t> m_mFileSizes;
};

} // namespace mocks
} // namespace services
} // namespace ra

#endif // !RA_SERVICES_MOCK_FILESYSTEM_HH