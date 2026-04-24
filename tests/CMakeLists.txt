# Copyright (c) 2026 juantgd. All Rights Reserved.

add_executable(acnet_mail_stress_test
  mail_stress_test.cc
)

target_link_libraries(acnet_mail_stress_test
  PRIVATE
    corelib
)

add_test(
  NAME acnet_mail_stress_test
  COMMAND $<TARGET_FILE:acnet_mail_stress_test>
)
