# Copyright (c) 2019, MariaDB corporation. All rights reserved.
# Copyright (c) 2026, lefred (Frédéric Descamps). All rights reserved.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1335  USA


MYSQL_ADD_PLUGIN(json_extra
                 plugin.cc json_extra.cc
                 MODULE_ONLY RECOMPILE_FOR_EMBEDDED)

ADD_EXECUTABLE(json_extra_test EXCLUDE_FROM_ALL
               tests/json_extra_test.cc json_extra.cc)
TARGET_INCLUDE_DIRECTORIES(json_extra_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
ADD_TEST(NAME json_extra COMMAND json_extra_test)
