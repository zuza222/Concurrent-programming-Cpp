/*
 * do pisania tego pliku korzystałam z tego źródła:
 * https://stackoverflow.com/questions/478898/how-do-i-execute-a-command-and-get-the-output-of-the-command-within-c-using-po
 */

#ifndef SRC_SHA256IDGENERATOR_HPP_
#define SRC_SHA256IDGENERATOR_HPP_

#include "immutable/idGenerator.hpp"
#include "immutable/pageId.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <mutex>

#define BUFFER_SIZE 64
#define READ_FD 0
#define WRITE_FD 1

class Sha256IdGenerator : public IdGenerator {
public:
    virtual PageId generateId(std::string const& content) const
    {
        int childToParent[2];
        int parentToChild[2];
        char buffer[BUFFER_SIZE + 1];

        std::unique_lock<std::mutex> lock(mutex);

        ASSERT(pipe2(childToParent, O_CLOEXEC) != -1, "Failed: create pipe");
        ASSERT(pipe2(parentToChild, O_CLOEXEC) != -1, "Failed: create pipe");

        int pid = fork();

        lock.unlock();

        switch (pid) {
        case -1:
            ASSERT(false, "Failed: fork");

        case 0:
            ASSERT(dup2(parentToChild[READ_FD], STDIN_FILENO) != -1,"Failed: dup2");
            ASSERT(dup2(childToParent[WRITE_FD], STDOUT_FILENO) != -1,"Failed: dup2");
            ASSERT(dup2(childToParent[WRITE_FD], STDERR_FILENO) != -1,"Failed: dup2");
            ASSERT(close(parentToChild[WRITE_FD]) != -1,"Failed: dup2");
            ASSERT(close(childToParent[READ_FD]) != -1,"Failed: dup2");

            execlp("sha256sum", "sha256sum", NULL);
            ASSERT(false, "Failed: execlp");

        default:
            ASSERT(close(parentToChild[READ_FD]) != -1,"Failed: close");
            ASSERT(close(childToParent[WRITE_FD]) != -1,"Failed: close pipe write");
            ASSERT(write(parentToChild[WRITE_FD], content.c_str(), content.length()) != -1,"Failed: write");
            ASSERT(close(parentToChild[WRITE_FD]) != -1,"Failed: close pipe");
            ASSERT(read(childToParent[READ_FD], buffer, BUFFER_SIZE + 1),"Failed: pipe read");

            buffer[BUFFER_SIZE] = '\0';
        }

        ASSERT(close(childToParent[READ_FD]) != -1, "Failed: pipe close");

        return std::string(buffer);
    }
private:
    mutable std::mutex mutex;
};

#endif /* SRC_SHA256IDGENERATOR_HPP_ */
