#ifndef SRC_MULTITHREADEDPAGERANKCOMPUTER_HPP_
#define SRC_MULTITHREADEDPAGERANKCOMPUTER_HPP_

#include <atomic>
#include <mutex>
#include <thread>

#include <future>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "immutable/network.hpp"
#include "immutable/pageIdAndRank.hpp"
#include "immutable/pageRankComputer.hpp"

class MultiThreadedPageRankComputer : public PageRankComputer {
    using numLinks_t = std::unordered_map<PageId, uint32_t, PageIdHash>;
    using pagesMap_t = std::unordered_map<PageId, PageRank, PageIdHash>;
    using edges_t = std::unordered_map<PageId, std::vector<PageId>, PageIdHash>;
    using danglingNodes_t = std::unordered_set<PageId, PageIdHash>;
    using vector = std::vector<double>;

public:
    MultiThreadedPageRankComputer(uint32_t numThreadsArg)
        : numThreads(numThreadsArg) {};

    std::vector<PageIdAndRank> computeForNetwork(Network const& network, double alpha, uint32_t iterations,
        double tolerance) const
    {
        initId(network);
        std::vector<double> pageRankDifferences(network.getPages().size());

        numLinks_t numLinks;
        pagesMap_t pageHashMap;
        edges_t edges;
        danglingNodes_t danglingNodes;
        std::vector<PageId> pageIds;

        initStructures(network, pageHashMap, numLinks, danglingNodes, edges, pageIds);

        double dangleSum = 1.0 * danglingNodes.size() / network.getSize();

        for (uint32_t i = 0; i < iterations; ++i) {
            double danglingWeight = alpha / network.getSize();
            double tmp = dangleSum * danglingWeight + (1.0 - alpha) / network.getSize();

            std::atomic<size_t> page(0);
            std::thread threads[numThreads];
            std::vector<double> differences(numThreads);
            std::vector<double> dangleSums(numThreads);

            for (uint32_t j = 0; j < numThreads; j++) {
                threads[j] = std::thread([&pageIds, &pageHashMap, &pageRankDifferences, &edges, &numLinks, &differences,
                                             &dangleSums, &page, j, alpha, tmp] {
                    double dangleSum = 0;
                    double difference = 0;
                    size_t first;

                    while ((first = page.fetch_add(5)) < pageIds.size()) {
                        for (size_t index = first; index < std::min(pageIds.size(), first + 5); index++) {
                            const PageId& pageId = pageIds[index];

                            double total = tmp;

                            if (edges.count(pageId) > 0) {
                                for (const auto& link : edges[pageId]) {
                                    total += alpha * pageHashMap[link] / numLinks[link];
                                }
                            }

                            pageRankDifferences[index] = total - pageHashMap[pageId];
                            difference += std::abs(pageRankDifferences[index]);

                            if (numLinks[pageId] == 0) {
                                dangleSum += total;
                            }
                        }
                    }
                    differences[j] = difference;
                    dangleSums[j] = dangleSum;
                });
            }

            for (auto& thread : threads) {
                thread.join();
            }

            initPageRankDifferences(pageIds, pageHashMap, pageRankDifferences);
            PageRank difference = 0.0;

            for (auto diff : differences) {
                difference += diff;
            }

            dangleSum = 0.0;

            for (auto sum : dangleSums) {
                dangleSum += sum;
            }

            if (difference < tolerance) {
                std::vector<PageIdAndRank> result;

                for (const auto& iter : pageHashMap) {
                    result.push_back(PageIdAndRank(iter.first, iter.second));
                }

                return result;
            }
        }

        ASSERT(false, "Not able to find result in iterations=" << iterations);
    }

    std::string getName() const
    {
        return "MultiThreadedPageRankComputer[" + std::to_string(this->numThreads) + "]";
    }

private:
    void initId(const Network& network) const
    {
        std::atomic<size_t> page(0);
        std::thread threads[numThreads];

        for (auto& thread : threads) {
            thread = std::thread([&] {
                size_t firstIndex;
                size_t networkSize = network.getPages().size();

                while ((firstIndex = page.fetch_add(5)) < networkSize) {
                    for (size_t index = firstIndex; index < std::min(networkSize, firstIndex + 5); index++) {
                        network.getPages()[index].generateId(network.getGenerator());
                    }
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }
    }

    static void makePages(const Network& network, pagesMap_t& pageHashMap)
    {
        for (auto const& page : network.getPages()) {
            pageHashMap[page.getId()] = 1.0 / network.getSize();
        }
    }

    static void makeNumLinks(const Network& network, numLinks_t& numLinks)
    {
        for (const auto& page : network.getPages()) {
            numLinks[page.getId()] = page.getLinks().size();
        }
    }

    static void makeDanglingNodes(const Network& network, danglingNodes_t& danglingNodes)
    {
        for (const auto& page : network.getPages()) {
            if (page.getLinks().size() == 0) {
                danglingNodes.insert(page.getId());
            }
        }
    }

    static void makeEdges(const Network& network, edges_t& edges)
    {
        for (const auto& page : network.getPages()) {
            for (const auto& link : page.getLinks()) {
                edges[link].push_back(page.getId());
            }
        }
    }

    static void makePageId(const Network& network, std::vector<PageId>& pageIds)
    {
        for (const auto& page : network.getPages()) {
            pageIds.push_back(page.getId());
        }
    }

    void initStructures(const Network& network, pagesMap_t& pageHashMap, numLinks_t& numLinks,
        danglingNodes_t& danglingNodes, edges_t& edges, std::vector<PageId>& pageIds) const
    {
        static const size_t counterTask = 5;
        std::atomic<size_t> currentTask(0);
        std::thread threads[threadsFilling(counterTask)];

        for (auto& thread : threads) {
            thread = std::thread([&] {
                size_t num;

                while ((num = currentTask.fetch_add(1)) < counterTask) {
                    if (num == 4) {
                        makePageId(network, pageIds);
                    } else if (num == 3) {
                        makeEdges(network, edges);
                    } else if (num == 2) {
                        makeDanglingNodes(network, danglingNodes);
                    } else if (num == 1) {
                        makeNumLinks(network, numLinks);
                    } else if (num == 0) {
                        makePages(network, pageHashMap);
                    }
                }
            });
        }

        for (auto& thread : threads) {
            thread.join();
        }
    }

    size_t threadsFilling(size_t counterTask) const
    {
        if (counterTask < numThreads) {
            return counterTask;
        } else {
            return numThreads;
        }
    }

    void initPageRankDifferences(std::vector<PageId>& pageIds, pagesMap_t& pageHashMap,
        vector& pageRankDifferences) const
    {
        std::atomic<size_t> currentPage(0);
        std::thread threads[numThreads];

        for (auto& thread : threads) {
            thread = std::thread(
                [&] {
                    size_t first;

                    while ((first = currentPage.fetch_add(5)) < pageIds.size()) {
                        for (size_t i = first; i < std::min(pageIds.size(), first + 5); i++) {
                            pageHashMap[pageIds[i]] += pageRankDifferences[i];
                        }
                    };
                });
        }
        for (auto& thread : threads) {
            thread.join();
        }
    }
    uint32_t numThreads;
};

#endif /* SRC_MULTITHREADEDPAGERANKCOMPUTER_HPP_ */
