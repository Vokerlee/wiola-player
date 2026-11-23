import unittest

from tools.architecture.review_contract.review import (
    ChangeReview,
    SECTIONS,
    validate_subject,
)


def review():
    return "\n".join(
        "## " + section + "\nConcrete contract with independent validation.\n"
        for section in SECTIONS
    )


class ChangeReviewTest(unittest.TestCase):
    def test_complete_review_and_explicit_retirement_are_reviewable(self):
        change = ChangeReview.read(
            review().replace(
                "## Removed code\n", "## Removed code\n`old/adapter.hpp` is retired.\n"
            )
        )
        self.assertEqual(
            change.reviewed_deletions(("old/adapter.hpp", "other.hpp")),
            frozenset({"old/adapter.hpp"}),
        )

    def test_empty_comments_placeholders_and_duplicate_sections_fail(self):
        for text in [
            "",
            review().replace(
                "Concrete contract with independent validation.",
                "<!-- explain here -->",
            ),
            review() + "\n## Validation\nTODO replace this text\n",
            review().replace("Concrete contract", "TODO contract"),
        ]:
            with self.subTest(text=text), self.assertRaises(ValueError):
                ChangeReview.read(text)

    def test_published_subjects_have_an_area_lowercase_and_pr_number(self):
        validate_subject("ci: qualify x64 Windows SDK caching (#435)", published=True)
        validate_subject("tests: preserve operation receipts", published=False)
        for subject in [
            "Preserve operation receipts (#436)",
            "tests: Preserve operation receipts (#436)",
            "tests: preserve operation receipts",
            "tests: preserve operation receipts. (#436)",
        ]:
            with self.subTest(subject=subject), self.assertRaises(ValueError):
                validate_subject(subject, published=True)
