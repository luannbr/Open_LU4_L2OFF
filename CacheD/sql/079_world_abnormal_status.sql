-- Current Cached.exe primary opcodes 109/110 abnormal-status persistence.
--
-- The image fixes the current callable ABI: an explicit delete taking one
-- character ID and a 42-parameter batch update containing two header values
-- plus eight five-field records.  Current bind order appends skill_sub_level
-- after the compatible donor's id/level/duration/selfcast prefix.  The
-- delete-then-insert-if-absent relational body is unanimous across the
-- modified-287, Ertheia and Classic 3.0 SQL donors. Those donors predate only
-- the current sublevel field and use twelve records instead of eight.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_deletenewactiveskill"(
    p_char_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_newactiveskill"
    WHERE "char_id" = p_char_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updatenewactiveskillbatch"(
    p_char_id integer,
    p_is_delete integer,
    p_skill_id1 integer,
    p_skill_level1 smallint,
    p_skill_duration1 integer,
    p_skill_selfcast1 integer,
    p_skill_sub_level1 smallint,
    p_skill_id2 integer,
    p_skill_level2 smallint,
    p_skill_duration2 integer,
    p_skill_selfcast2 integer,
    p_skill_sub_level2 smallint,
    p_skill_id3 integer,
    p_skill_level3 smallint,
    p_skill_duration3 integer,
    p_skill_selfcast3 integer,
    p_skill_sub_level3 smallint,
    p_skill_id4 integer,
    p_skill_level4 smallint,
    p_skill_duration4 integer,
    p_skill_selfcast4 integer,
    p_skill_sub_level4 smallint,
    p_skill_id5 integer,
    p_skill_level5 smallint,
    p_skill_duration5 integer,
    p_skill_selfcast5 integer,
    p_skill_sub_level5 smallint,
    p_skill_id6 integer,
    p_skill_level6 smallint,
    p_skill_duration6 integer,
    p_skill_selfcast6 integer,
    p_skill_sub_level6 smallint,
    p_skill_id7 integer,
    p_skill_level7 smallint,
    p_skill_duration7 integer,
    p_skill_selfcast7 integer,
    p_skill_sub_level7 smallint,
    p_skill_id8 integer,
    p_skill_level8 smallint,
    p_skill_duration8 integer,
    p_skill_selfcast8 integer,
    p_skill_sub_level8 smallint
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_is_delete = 1 THEN
        DELETE FROM "world"."user_newactiveskill"
        WHERE "char_id" = p_char_id;
    END IF;

    IF p_skill_id1 > 0 AND NOT EXISTS (
        SELECT 1 FROM "world"."user_newactiveskill"
        WHERE "char_id" = p_char_id AND "skill_id" = p_skill_id1
    ) THEN
        INSERT INTO "world"."user_newactiveskill" (
            "char_id", "skill_id", "skill_level", "skill_duration",
            "skill_selfcast", "skill_sub_level"
        ) VALUES (
            p_char_id, p_skill_id1, p_skill_level1, p_skill_duration1,
            p_skill_selfcast1, p_skill_sub_level1
        );
    END IF;

    IF p_skill_id2 > 0 AND NOT EXISTS (
        SELECT 1 FROM "world"."user_newactiveskill"
        WHERE "char_id" = p_char_id AND "skill_id" = p_skill_id2
    ) THEN
        INSERT INTO "world"."user_newactiveskill" (
            "char_id", "skill_id", "skill_level", "skill_duration",
            "skill_selfcast", "skill_sub_level"
        ) VALUES (
            p_char_id, p_skill_id2, p_skill_level2, p_skill_duration2,
            p_skill_selfcast2, p_skill_sub_level2
        );
    END IF;

    IF p_skill_id3 > 0 AND NOT EXISTS (
        SELECT 1 FROM "world"."user_newactiveskill"
        WHERE "char_id" = p_char_id AND "skill_id" = p_skill_id3
    ) THEN
        INSERT INTO "world"."user_newactiveskill" (
            "char_id", "skill_id", "skill_level", "skill_duration",
            "skill_selfcast", "skill_sub_level"
        ) VALUES (
            p_char_id, p_skill_id3, p_skill_level3, p_skill_duration3,
            p_skill_selfcast3, p_skill_sub_level3
        );
    END IF;

    IF p_skill_id4 > 0 AND NOT EXISTS (
        SELECT 1 FROM "world"."user_newactiveskill"
        WHERE "char_id" = p_char_id AND "skill_id" = p_skill_id4
    ) THEN
        INSERT INTO "world"."user_newactiveskill" (
            "char_id", "skill_id", "skill_level", "skill_duration",
            "skill_selfcast", "skill_sub_level"
        ) VALUES (
            p_char_id, p_skill_id4, p_skill_level4, p_skill_duration4,
            p_skill_selfcast4, p_skill_sub_level4
        );
    END IF;

    IF p_skill_id5 > 0 AND NOT EXISTS (
        SELECT 1 FROM "world"."user_newactiveskill"
        WHERE "char_id" = p_char_id AND "skill_id" = p_skill_id5
    ) THEN
        INSERT INTO "world"."user_newactiveskill" (
            "char_id", "skill_id", "skill_level", "skill_duration",
            "skill_selfcast", "skill_sub_level"
        ) VALUES (
            p_char_id, p_skill_id5, p_skill_level5, p_skill_duration5,
            p_skill_selfcast5, p_skill_sub_level5
        );
    END IF;

    IF p_skill_id6 > 0 AND NOT EXISTS (
        SELECT 1 FROM "world"."user_newactiveskill"
        WHERE "char_id" = p_char_id AND "skill_id" = p_skill_id6
    ) THEN
        INSERT INTO "world"."user_newactiveskill" (
            "char_id", "skill_id", "skill_level", "skill_duration",
            "skill_selfcast", "skill_sub_level"
        ) VALUES (
            p_char_id, p_skill_id6, p_skill_level6, p_skill_duration6,
            p_skill_selfcast6, p_skill_sub_level6
        );
    END IF;

    IF p_skill_id7 > 0 AND NOT EXISTS (
        SELECT 1 FROM "world"."user_newactiveskill"
        WHERE "char_id" = p_char_id AND "skill_id" = p_skill_id7
    ) THEN
        INSERT INTO "world"."user_newactiveskill" (
            "char_id", "skill_id", "skill_level", "skill_duration",
            "skill_selfcast", "skill_sub_level"
        ) VALUES (
            p_char_id, p_skill_id7, p_skill_level7, p_skill_duration7,
            p_skill_selfcast7, p_skill_sub_level7
        );
    END IF;

    IF p_skill_id8 > 0 AND NOT EXISTS (
        SELECT 1 FROM "world"."user_newactiveskill"
        WHERE "char_id" = p_char_id AND "skill_id" = p_skill_id8
    ) THEN
        INSERT INTO "world"."user_newactiveskill" (
            "char_id", "skill_id", "skill_level", "skill_duration",
            "skill_selfcast", "skill_sub_level"
        ) VALUES (
            p_char_id, p_skill_id8, p_skill_level8, p_skill_duration8,
            p_skill_selfcast8, p_skill_sub_level8
        );
    END IF;
END;
$function$;

COMMIT;
