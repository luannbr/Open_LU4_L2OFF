-- Extended requests 193 and 196-198. Current Cached.exe fixes the callable
-- parameter types and result order. The compatible donor families fix the
-- BlockIP transitions, summon upsert and delete effects. The current summon
-- batch is newer than every supplied donor: it binds 8 records of five fields
-- and persists skill_sublevel, so that column and 44-parameter surface follow
-- the current machine ABI instead of the incompatible 12-by-4 donor ABI.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

ALTER TABLE "world"."user_summon_new_abnormal"
    ADD COLUMN IF NOT EXISTS "skill_sublevel" smallint NOT NULL DEFAULT 0;

CREATE OR REPLACE FUNCTION "world"."lin_getblockedip"(
    p_status integer
) RETURNS TABLE (
    startnum bigint,
    endnum bigint
)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_status = 0 THEN
        RETURN QUERY
        SELECT blocked."startnum", blocked."endnum"
        FROM "world"."blockip" AS blocked
        WHERE blocked."status" = 0
        ORDER BY blocked."startnum";

        DELETE FROM "world"."blockip"
        WHERE "status" = 0;
    ELSIF p_status = 1 THEN
        RETURN QUERY
        SELECT blocked."startnum", blocked."endnum"
        FROM "world"."blockip" AS blocked
        WHERE blocked."status" = 1
        ORDER BY blocked."startnum";

        UPDATE "world"."blockip"
        SET "status" = 2
        WHERE "status" = 1;
    ELSIF p_status = 2 THEN
        RETURN QUERY
        SELECT blocked."startnum", blocked."endnum"
        FROM "world"."blockip" AS blocked
        WHERE blocked."status" > 0
        ORDER BY blocked."startnum";

        DELETE FROM "world"."blockip"
        WHERE "status" = 0;

        UPDATE "world"."blockip"
        SET "status" = 2
        WHERE "status" = 1;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_deletesummonnewabnormalstate"(
    p_char_id integer,
    p_summon_class_id integer,
    p_summon_db_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    DELETE FROM "world"."user_summon_new_abnormal" AS abnormal
    WHERE abnormal."char_id" = p_char_id
      AND abnormal."summon_class_id" = p_summon_class_id
      AND abnormal."summon_db_id" = p_summon_db_id;
$function$;

CREATE OR REPLACE FUNCTION
"world"."lin_updateorinsertsummonnewabnormalstatebatch"(
    p_char_id integer,
    p_summon_class_id integer,
    p_summon_db_id integer,
    p_is_delete integer,
    p_skill_id1 integer,
    p_skill_level1 smallint,
    p_skill_duration1 integer,
    p_skill_selfcast1 integer,
    p_skill_sublevel1 smallint,
    p_skill_id2 integer,
    p_skill_level2 smallint,
    p_skill_duration2 integer,
    p_skill_selfcast2 integer,
    p_skill_sublevel2 smallint,
    p_skill_id3 integer,
    p_skill_level3 smallint,
    p_skill_duration3 integer,
    p_skill_selfcast3 integer,
    p_skill_sublevel3 smallint,
    p_skill_id4 integer,
    p_skill_level4 smallint,
    p_skill_duration4 integer,
    p_skill_selfcast4 integer,
    p_skill_sublevel4 smallint,
    p_skill_id5 integer,
    p_skill_level5 smallint,
    p_skill_duration5 integer,
    p_skill_selfcast5 integer,
    p_skill_sublevel5 smallint,
    p_skill_id6 integer,
    p_skill_level6 smallint,
    p_skill_duration6 integer,
    p_skill_selfcast6 integer,
    p_skill_sublevel6 smallint,
    p_skill_id7 integer,
    p_skill_level7 smallint,
    p_skill_duration7 integer,
    p_skill_selfcast7 integer,
    p_skill_sublevel7 smallint,
    p_skill_id8 integer,
    p_skill_level8 smallint,
    p_skill_duration8 integer,
    p_skill_selfcast8 integer,
    p_skill_sublevel8 smallint
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    IF p_is_delete = 1 THEN
        DELETE FROM "world"."user_summon_new_abnormal" AS abnormal
        WHERE abnormal."char_id" = p_char_id
          AND abnormal."summon_class_id" = p_summon_class_id
          AND abnormal."summon_db_id" = p_summon_db_id;
    END IF;

    IF p_skill_id1 > 0 THEN
        INSERT INTO "world"."user_summon_new_abnormal" (
            "char_id", "summon_class_id", "summon_db_id", "skill_id",
            "skill_level", "skill_duration", "skill_selfcast",
            "skill_sublevel"
        ) VALUES (
            p_char_id, p_summon_class_id, p_summon_db_id, p_skill_id1,
            p_skill_level1, p_skill_duration1, p_skill_selfcast1::smallint,
            p_skill_sublevel1
        ) ON CONFLICT DO NOTHING;
    END IF;
    IF p_skill_id2 > 0 THEN
        INSERT INTO "world"."user_summon_new_abnormal" (
            "char_id", "summon_class_id", "summon_db_id", "skill_id",
            "skill_level", "skill_duration", "skill_selfcast",
            "skill_sublevel"
        ) VALUES (
            p_char_id, p_summon_class_id, p_summon_db_id, p_skill_id2,
            p_skill_level2, p_skill_duration2, p_skill_selfcast2::smallint,
            p_skill_sublevel2
        ) ON CONFLICT DO NOTHING;
    END IF;
    IF p_skill_id3 > 0 THEN
        INSERT INTO "world"."user_summon_new_abnormal" (
            "char_id", "summon_class_id", "summon_db_id", "skill_id",
            "skill_level", "skill_duration", "skill_selfcast",
            "skill_sublevel"
        ) VALUES (
            p_char_id, p_summon_class_id, p_summon_db_id, p_skill_id3,
            p_skill_level3, p_skill_duration3, p_skill_selfcast3::smallint,
            p_skill_sublevel3
        ) ON CONFLICT DO NOTHING;
    END IF;
    IF p_skill_id4 > 0 THEN
        INSERT INTO "world"."user_summon_new_abnormal" (
            "char_id", "summon_class_id", "summon_db_id", "skill_id",
            "skill_level", "skill_duration", "skill_selfcast",
            "skill_sublevel"
        ) VALUES (
            p_char_id, p_summon_class_id, p_summon_db_id, p_skill_id4,
            p_skill_level4, p_skill_duration4, p_skill_selfcast4::smallint,
            p_skill_sublevel4
        ) ON CONFLICT DO NOTHING;
    END IF;
    IF p_skill_id5 > 0 THEN
        INSERT INTO "world"."user_summon_new_abnormal" (
            "char_id", "summon_class_id", "summon_db_id", "skill_id",
            "skill_level", "skill_duration", "skill_selfcast",
            "skill_sublevel"
        ) VALUES (
            p_char_id, p_summon_class_id, p_summon_db_id, p_skill_id5,
            p_skill_level5, p_skill_duration5, p_skill_selfcast5::smallint,
            p_skill_sublevel5
        ) ON CONFLICT DO NOTHING;
    END IF;
    IF p_skill_id6 > 0 THEN
        INSERT INTO "world"."user_summon_new_abnormal" (
            "char_id", "summon_class_id", "summon_db_id", "skill_id",
            "skill_level", "skill_duration", "skill_selfcast",
            "skill_sublevel"
        ) VALUES (
            p_char_id, p_summon_class_id, p_summon_db_id, p_skill_id6,
            p_skill_level6, p_skill_duration6, p_skill_selfcast6::smallint,
            p_skill_sublevel6
        ) ON CONFLICT DO NOTHING;
    END IF;
    IF p_skill_id7 > 0 THEN
        INSERT INTO "world"."user_summon_new_abnormal" (
            "char_id", "summon_class_id", "summon_db_id", "skill_id",
            "skill_level", "skill_duration", "skill_selfcast",
            "skill_sublevel"
        ) VALUES (
            p_char_id, p_summon_class_id, p_summon_db_id, p_skill_id7,
            p_skill_level7, p_skill_duration7, p_skill_selfcast7::smallint,
            p_skill_sublevel7
        ) ON CONFLICT DO NOTHING;
    END IF;
    IF p_skill_id8 > 0 THEN
        INSERT INTO "world"."user_summon_new_abnormal" (
            "char_id", "summon_class_id", "summon_db_id", "skill_id",
            "skill_level", "skill_duration", "skill_selfcast",
            "skill_sublevel"
        ) VALUES (
            p_char_id, p_summon_class_id, p_summon_db_id, p_skill_id8,
            p_skill_level8, p_skill_duration8, p_skill_selfcast8::smallint,
            p_skill_sublevel8
        ) ON CONFLICT DO NOTHING;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_loadsummonabnormalstate"(
    p_char_id integer,
    p_summon_class_id integer,
    p_summon_db_id integer
) RETURNS TABLE (
    skill_id integer,
    skill_level integer,
    skill_sublevel integer,
    skill_duration integer,
    skill_selfcast integer
)
LANGUAGE sql
STABLE
AS $function$
    SELECT abnormal."skill_id",
           abnormal."skill_level"::integer,
           abnormal."skill_sublevel"::integer,
           abnormal."skill_duration",
           abnormal."skill_selfcast"::integer
    FROM "world"."user_summon_new_abnormal" AS abnormal
    WHERE abnormal."char_id" = p_char_id
      AND abnormal."summon_class_id" = p_summon_class_id
      AND abnormal."summon_db_id" = p_summon_db_id;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_updateusersummon"(
    p_char_id integer,
    p_summon_class_id integer,
    p_summon_db_id integer,
    p_summon_level integer,
    p_summon_org_remain_time integer,
    p_summon_remain_time integer,
    p_summon_exp_rate integer,
    p_summon_hp double precision,
    p_summon_mp double precision,
    p_summon_consume_item_id integer,
    p_summon_consume_item_count integer,
    p_summon_is_summon integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."user_summon_data" AS summon (
        "char_id",
        "summon_class_id",
        "summon_db_id",
        "summon_level",
        "summon_org_remain_time",
        "summon_remain_time",
        "summon_exp_rate",
        "summon_hp",
        "summon_mp",
        "summon_consume_item_id",
        "summon_consume_item_count",
        "summon_is_summon"
    ) VALUES (
        p_char_id,
        p_summon_class_id,
        p_summon_db_id,
        p_summon_level,
        p_summon_org_remain_time,
        p_summon_remain_time,
        p_summon_exp_rate::smallint,
        p_summon_hp,
        p_summon_mp,
        p_summon_consume_item_id,
        p_summon_consume_item_count,
        p_summon_is_summon
    )
    ON CONFLICT ("char_id", "summon_db_id") DO UPDATE
    SET "summon_class_id" = EXCLUDED."summon_class_id",
        "summon_db_id" = EXCLUDED."summon_db_id",
        "summon_level" = EXCLUDED."summon_level",
        "summon_org_remain_time" = EXCLUDED."summon_org_remain_time",
        "summon_remain_time" = EXCLUDED."summon_remain_time",
        "summon_exp_rate" = EXCLUDED."summon_exp_rate",
        "summon_hp" = EXCLUDED."summon_hp",
        "summon_mp" = EXCLUDED."summon_mp",
        "summon_consume_item_id" = EXCLUDED."summon_consume_item_id",
        "summon_consume_item_count" = EXCLUDED."summon_consume_item_count",
        "summon_is_summon" = EXCLUDED."summon_is_summon";
$function$;

COMMIT;
