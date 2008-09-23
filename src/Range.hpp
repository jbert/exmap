/*
 * (c) John Berthels 2005 <jjberthels@gmail.com>. See COPYING for license.
 */
#ifndef _RANGE_H
#define _RANGE_H

#include <string>
#include <list>

#include <boost/shared_ptr.hpp>

/// Handle manipulation of half-open (start <= x < end) ranges
class Range;
typedef boost::shared_ptr<Range> RangePtr;
class Range
{
public:
    Range(unsigned long start, unsigned long end);
    unsigned long start() const;
    unsigned long end() const;
    unsigned long size() const;
    
    RangePtr intersect(const Range &r) const;
    bool operator==(const Range &r) const;
    bool operator<(const Range &other);
    
    RangePtr add(unsigned long v) const;
    RangePtr subtract(unsigned long v) const;
    bool contains(unsigned long v) const;
    bool contains(const Range &r) const;
    bool contains(const RangePtr &r) const;
    bool overlaps(const Range &r) const;
    bool overlaps(const RangePtr &r) const;
    RangePtr truncate_below(unsigned long v) const;
    RangePtr truncate_above(unsigned long v) const;
    RangePtr merge(const Range &r) const;
    std::string to_string() const;
    void print(std::ostream &os) const;

    std::list<RangePtr> invert_list(const std::list<RangePtr> &l);
    
    static std::list<RangePtr> merge_list(const std::list<RangePtr> &l);

    /// True if any of the ranges overlap
    static bool any_overlap(const std::list<RangePtr> &l);
    
private:
    unsigned long _start;
    unsigned long _end;
};

std::ostream &operator<<(std::ostream &os, const Range &r);
std::ostream &operator<<(std::ostream &os, const RangePtr &r);

// This probably deserves some explanation. boost::shared_ptr provides
// a template for operator< which is based on the number of reference
// counts of the shared pointers. This isn't really useful. The
// following template specialisation (naughtily placed into the boost
// namespace) overrides this with a method which proxies the
// comparison to the pointed-to object. This means that (amongst other
// things) you can .sort() a list of boost::shared_ptrs by their
// underlying class.
namespace boost {
    
template<> inline
bool operator< <Range, Range>(boost::shared_ptr<Range> const &a,
			      boost::shared_ptr<Range> const &b)
{
    return *a < *b;
}

};


#endif
