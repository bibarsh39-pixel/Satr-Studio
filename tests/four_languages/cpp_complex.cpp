#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <numeric>
#include <queue>
#include <regex>
#include <vector>
template<class T> T total(const std::vector<T>& values){return std::accumulate(values.begin(),values.end(),T{});}
int main(){
    const int n=2000;std::vector<std::vector<std::pair<int,int>>> graph(n);
    for(int i=0;i<n-1;++i){graph[i].push_back({i+1,1});if(i+5<n)graph[i].push_back({i+5,8});}
    std::priority_queue<std::pair<int,int>,std::vector<std::pair<int,int>>,std::greater<>> queue;
    std::vector<int> distance(n,1000000);distance[0]=0;queue.push({0,0});
    while(!queue.empty()){auto [cost,v]=queue.top();queue.pop();if(cost!=distance[v])continue;for(auto [next,w]:graph[v])if(cost+w<distance[next]){distance[next]=cost+w;queue.push({cost+w,next});}}
    assert(distance.back()==1999);
    std::vector<std::future<long long>> jobs;
    for(int i=0;i<4;i++)jobs.push_back(std::async(std::launch::async,[i]{std::vector<long long> values(25000);std::iota(values.begin(),values.end(),i*25000);return total(values);}));
    long long sum=0;for(auto& job:jobs)sum+=job.get();assert(sum==4999950000LL);
    std::ofstream("cpp-result.txt")<<sum;assert(std::filesystem::file_size("cpp-result.txt")==10);
    assert(std::regex_match("Satr-2026",std::regex("[A-Za-z]+-[0-9]+")));
    std::cout<<"CPP_OK:"<<sum<<'\n';
}
