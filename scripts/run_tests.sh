#! /bin/bash

# . ${basename BASH_SOURCE}/preamble.sh
# echo "$(dirname ${BASH_SOURCE[0]})/preamble.sh"
source "$(dirname ${BASH_SOURCE[0]})/preamble.sh"

EXIT_CODE=0
TESTS_PASSED=0

# echo "${BASH_SOURCE[0]}"
# exit 0

if [ "$Platform" == "Linux" ] || [ "$Platform" == "macOS" ] ; then
  # NOTE(nsillik)(macos): find and -type f rather than a ./bin/tests/* glob,
  # because clang writes a .dSYM bundle *directory* beside every binary on macOS.
  # The glob matches it, the loop tries to execute it, and a fully-passing build
  # reports one spurious suite failure per binary.
  exe_search_string='./bin/tests -maxdepth 1 -type f -perm -u+x';
elif [[ "$Platform" == "Windows" ]] ; then
  # TODO(Jesse): Do we actually need this since switching off VS?  Does clang
  # output pdb files there or something?
  exe_search_string='./bin/tests/*.exe';
fi

echo $(pwd)
for test_executable in $(find $exe_search_string); do
  # NOTE(nsillik): This read `$COLORFLAG == 0`, neither of which exists.  The
  # color flag is POOF_COLOR_FLAG (preamble.sh) and the test binaries ignore
  # argv, so `== 0` was passed to them as literal junk.  What the branch wants is
  # the exit status, and that is all it needs.
  if $test_executable $POOF_COLOR_FLAG; then
    TESTS_PASSED=$((TESTS_PASSED+1))
    echo -n ""
  else
    EXIT_CODE=$(($EXIT_CODE+1))
  fi
done

if [ "$EXIT_CODE" -eq 0 ]; then
  echo ""
  # NOTE(nsillik): TESTES_PASSED was a typo, so this always printed "All Tests () Passed".
  echo "All Tests ($TESTS_PASSED) Passed"
elif [ "$EXIT_CODE" -eq 1 ]; then
  echo ""
  echo "$EXIT_CODE Test suite failed. Inspect log for details."
else
  echo ""
  echo "$EXIT_CODE Test suites failed. Inspect log for details."
fi

exit $EXIT_CODE
