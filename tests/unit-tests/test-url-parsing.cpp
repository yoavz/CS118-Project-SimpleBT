/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2014,  Regents of the University of California
 *
 * This file is part of Simple BT.
 * See AUTHORS.md for complete list of Simple BT authors and contributors.
 *
 * NSL is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * NSL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * NSL, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 *
 * \author Yingdi Yu <yingdi@cs.ucla.edu>
 */

#include <http/url-parsing.hpp>

#include "boost-test.hpp"

namespace sbt {
namespace test {

BOOST_AUTO_TEST_CASE(TestUrl1)
{
  Url url1("https://localhost:12345/announce.php");

  BOOST_CHECK_EQUAL(url1.getProtocol(), "https");
  BOOST_CHECK_EQUAL(url1.getHost(), "localhost");
  BOOST_CHECK_EQUAL(url1.getPort(), 12345);
  BOOST_CHECK_EQUAL(url1.getPath(), "/announce.php");
}

BOOST_AUTO_TEST_CASE(TestUrl2)
{
  Url url1("http://www.google.com:80/index.html");

  BOOST_CHECK_EQUAL(url1.getProtocol(), "http");
  BOOST_CHECK_EQUAL(url1.getHost(), "www.google.com");
  BOOST_CHECK_EQUAL(url1.getPort(), 80);
  BOOST_CHECK_EQUAL(url1.getPath(), "/index.html");
}

BOOST_AUTO_TEST_SUITE_END()

} // namespace sbt
