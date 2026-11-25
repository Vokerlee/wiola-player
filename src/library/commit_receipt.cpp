/**
 * @file
 * @brief Owner-thread durability acknowledgments with isolated observer failures.
 * @author Roman Glaz
 * @copyright (c) 2026, <vokerlee@gmail.com>
 *
 * Wiola is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Wiola is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Wiola. If not, see <http://www.gnu.org/licenses/>.
 */

#include "commit_receipt.hpp"
#include <exception>
#include <stdexcept>

namespace wiola::library {

void CommitReceipt::observe(Observer observer)
{
    if (std::this_thread::get_id() != owner_)
        throw std::logic_error{"commit receipt accessed outside owner thread"};
    if (!observer || cancelled_)
        return;
    if (durable_) {
        observer({revision_, true});
        return;
    }
    observers_.push_back(observer);
    if (failed_)
        observer({revision_, false});
}

void CommitReceipt::notify(bool durable)
{
    if (std::this_thread::get_id() != owner_)
        throw std::logic_error{"commit receipt resolved outside owner thread"};
    if (durable_ || cancelled_)
        return;
    durable_ = durable;
    failed_ = !durable;
    auto observers = durable ? std::move(observers_) : observers_;
    std::exception_ptr failure;
    for (const auto& observer : observers) {
        if (cancelled_ || (!durable && durable_))
            break;
        try {
            observer({revision_, durable});
        } catch (...) {
            if (!failure)
                failure = std::current_exception();
        }
    }
    if (failure)
        std::rethrow_exception(failure);
}

void CommitReceipt::cancel()
{
    if (std::this_thread::get_id() != owner_)
        throw std::logic_error{"commit receipt cancelled outside owner thread"};
    cancelled_ = true;
    observers_.clear();
}

} // namespace wiola::library
