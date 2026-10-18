-- Current Cached.exe AQ 43/49 character-copy and item-restore persistence.
--
-- The five/four/three-input call order and the 1/23/0-result ABIs come from
-- current Cached.exe DBConn bind sites. Classic 3.0, modified protocol-287
-- and Ertheia agree on the relational bodies. The two hlocked/use_count
-- item fields are the additional current ABI columns already established by
-- CItem and lin_CopyItemByDeletedChar.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

CREATE OR REPLACE FUNCTION "world"."lin_copychar"(
    p_source_char_id integer,
    p_new_char_name text,
    p_account_id integer,
    p_account_name text,
    p_builder integer
) RETURNS TABLE (char_id integer)
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_new_char_name text := rtrim(p_new_char_name);
    v_reserved_name varchar(50);
    v_reserved_account_id integer;
    v_source record;
    v_source_item record;
    v_new_char_id integer := 0;
    v_new_item_id integer;
BEGIN
    -- This is the surviving donor condition unchanged after RTRIM. It is not
    -- widened to an empty-string check because the target validates the name
    -- before entering this procedure as well.
    IF v_new_char_name LIKE ' ' THEN
        RAISE EXCEPTION
            'Character name has space: name = [%]', v_new_char_name;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_prohibit" AS prohibited
        WHERE "world"."mssql_text_eq"(
            prohibited."char_name",
            v_new_char_name
        )
    ) THEN
        RAISE EXCEPTION
            'Character name is prohibited: name = [%]', v_new_char_name;
    END IF;

    IF EXISTS (
        SELECT 1
        FROM "world"."user_prohibit_word" AS prohibited
        WHERE position(
            lower(prohibited."words") IN lower(v_new_char_name)) > 0
    ) THEN
        RAISE EXCEPTION
            'Character name has a prohibited word: name = [%]',
            v_new_char_name;
    END IF;

    SELECT reservation."char_name", reservation."account_id"
    INTO v_reserved_name, v_reserved_account_id
    FROM "world"."user_name_reserved" AS reservation
    WHERE reservation."used" = 0
      AND "world"."mssql_text_eq"(
          reservation."char_name",
          v_new_char_name
      )
    LIMIT 1;

    IF v_reserved_name IS NOT NULL
       AND v_reserved_account_id <> p_account_id THEN
        RAISE EXCEPTION
            'Character name is reserved by another player: name = [%]',
            v_new_char_name;
    END IF;

    SELECT source_character.*
    INTO v_source
    FROM "world"."user_data" AS source_character
    WHERE source_character."char_id" = p_source_char_id;

    IF NOT FOUND THEN
        RETURN QUERY SELECT v_new_char_id;
        RETURN;
    END IF;

    -- The compatible donor bodies replace the incoming builder
    -- with the source character's builder. Keep that historical behavior.
    -- p_builder remains part of the current five-argument wire ABI.
    INSERT INTO "world"."user_data" AS new_character (
        "char_name", "account_name", "account_id", "pledge_id",
        "create_date", "builder", "gender", "race", "class", "world",
        "xloc", "yloc", "zloc", "hp", "mp", "sp", "exp", "lev",
        "align", "pk", "pkpardon", "duel", "quest_flag", "max_hp",
        "max_mp", "quest_memo", "face_index", "hair_shape_index",
        "hair_color_index", "drop_exp", "subjob_id", "cp", "max_cp",
        "subjob0_class", "subjob1_class", "subjob2_class",
        "subjob3_class", "dual_class_id"
    ) VALUES (
        v_new_char_name, p_account_name, p_account_id, 0,
        "world"."mssql_getdate"(), v_source."builder", v_source."gender",
        v_source."race", v_source."class", v_source."world",
        v_source."xloc", v_source."yloc", v_source."zloc",
        v_source."hp", v_source."mp", v_source."sp", v_source."exp",
        v_source."lev", v_source."align", v_source."pk",
        v_source."pkpardon", v_source."duel", v_source."quest_flag",
        v_source."max_hp", v_source."max_mp", v_source."quest_memo",
        v_source."face_index", v_source."hair_shape_index",
        v_source."hair_color_index", v_source."drop_exp",
        v_source."subjob_id", v_source."cp", v_source."max_cp",
        v_source."subjob0_class", v_source."subjob1_class",
        v_source."subjob2_class", v_source."subjob3_class",
        v_source."dual_class_id"
    )
    RETURNING new_character."char_id" INTO v_new_char_id;

    INSERT INTO "world"."user_skill" (
        "char_id", "skill_id", "skill_lev", "to_end_time", "subjob_id",
        "is_lock", "skill_delay", "skill_sub_level"
    )
    SELECT v_new_char_id, skill."skill_id", skill."skill_lev",
           skill."to_end_time", skill."subjob_id", skill."is_lock",
           skill."skill_delay", skill."skill_sub_level"
    FROM "world"."user_skill" AS skill
    WHERE skill."char_id" = p_source_char_id;

    INSERT INTO "world"."user_skill_reuse_delay" (
        "char_id", "skill_id", "to_end_time", "subjob_id", "skill_delay"
    )
    SELECT v_new_char_id, reuse."skill_id", reuse."to_end_time",
           reuse."subjob_id", reuse."skill_delay"
    FROM "world"."user_skill_reuse_delay" AS reuse
    WHERE reuse."char_id" = p_source_char_id;

    INSERT INTO "world"."user_quest" (
        "char_id", "quest_no", "journal", "state1", "state2", "state3",
        "state4", "type"
    )
    SELECT v_new_char_id, quest."quest_no", quest."journal",
           quest."state1", quest."state2", quest."state3",
           quest."state4", quest."type"
    FROM "world"."user_quest" AS quest
    WHERE quest."char_id" = p_source_char_id;

    INSERT INTO "world"."user_npc_log" (
        "char_id", "quest_no", "npc_id", "type", "count"
    )
    SELECT v_new_char_id, npc_log."quest_no", npc_log."npc_id",
           npc_log."type", npc_log."count"
    FROM "world"."user_npc_log" AS npc_log
    WHERE npc_log."char_id" = p_source_char_id;

    INSERT INTO "world"."user_subjob" (
        "char_id", "hp", "mp", "sp", "exp", "level", "henna_1",
        "henna_2", "henna_3", "subjob_id", "create_date", "max_level",
        "pp"
    )
    SELECT v_new_char_id, subjob."hp", subjob."mp", subjob."sp",
           subjob."exp", subjob."level", subjob."henna_1",
           subjob."henna_2", subjob."henna_3", subjob."subjob_id",
           "world"."mssql_getdate"(), subjob."max_level", subjob."pp"
    FROM "world"."user_subjob" AS subjob
    WHERE subjob."char_id" = p_source_char_id;

    INSERT INTO "world"."user_henna" (
        "char_id", "henna_1", "henna_2", "henna_3", "subjob_id"
    )
    SELECT v_new_char_id, henna."henna_1", henna."henna_2",
           henna."henna_3", henna."subjob_id"
    FROM "world"."user_henna" AS henna
    WHERE henna."char_id" = p_source_char_id;

    INSERT INTO "world"."shortcut_data" (
        "char_id", "slotnum", "shortcut_type", "shortcut_id",
        "shortcut_macro", "subjob_id"
    )
    SELECT v_new_char_id, shortcut."slotnum", shortcut."shortcut_type",
           shortcut."shortcut_id", shortcut."shortcut_macro",
           shortcut."subjob_id"
    FROM "world"."shortcut_data" AS shortcut
    WHERE shortcut."char_id" = p_source_char_id;

    INSERT INTO "world"."user_nobless" (
        "char_id", "nobless_type", "hero_type", "win_count",
        "previous_point", "olympiad_point", "match_count", "words",
        "olympiad_win_count", "olympiad_lose_count", "history_open",
        "trade_point"
    )
    SELECT v_new_char_id, nobless."nobless_type", nobless."hero_type",
           nobless."win_count", nobless."previous_point",
           nobless."olympiad_point", nobless."match_count",
           nobless."words", nobless."olympiad_win_count",
           nobless."olympiad_lose_count", nobless."history_open",
           nobless."trade_point"
    FROM "world"."user_nobless" AS nobless
    WHERE nobless."char_id" = p_source_char_id;

    INSERT INTO "world"."user_job_group" ("char_id", "job_group")
    SELECT v_new_char_id, job_group."job_group"
    FROM "world"."user_job_group" AS job_group
    WHERE job_group."char_id" = p_source_char_id;

    -- The donor procedure assigns a fresh ID to each non-pet/non-racing item
    -- and then copies attribute, duration and period rows through that map.
    FOR v_source_item IN
        SELECT item."item_type", item."amount", item."enchant",
               item."eroded", item."bless", item."ident", item."wished",
               item."warehouse", item."variation_opt1",
               item."variation_opt2", item."intensive_item_type",
               item."hlocked", item."use_count",
               COALESCE(attribute."attack_attribute_type", -1)::smallint
                   AS attack_attribute_type,
               COALESCE(attribute."attack_attribute_value", -1)::smallint
                   AS attack_attribute_value,
               COALESCE(attribute."defend_attribute_0", -1)::smallint
                   AS defend_attribute_0,
               COALESCE(attribute."defend_attribute_1", -1)::smallint
                   AS defend_attribute_1,
               COALESCE(attribute."defend_attribute_2", -1)::smallint
                   AS defend_attribute_2,
               COALESCE(attribute."defend_attribute_3", -1)::smallint
                   AS defend_attribute_3,
               COALESCE(attribute."defend_attribute_4", -1)::smallint
                   AS defend_attribute_4,
               COALESCE(attribute."defend_attribute_5", -1)::smallint
                   AS defend_attribute_5,
               COALESCE(period."start_time", -1) AS start_time,
               COALESCE(period."period", -1) AS period,
               COALESCE(duration."duration", -1) AS duration
        FROM "world"."user_item" AS item
        LEFT JOIN "world"."user_item_attribute" AS attribute
          ON attribute."item_id" = item."item_id"
        LEFT JOIN "world"."user_item_period" AS period
          ON period."item_id" = item."item_id"
        LEFT JOIN "world"."user_item_duration" AS duration
          ON duration."item_id" = item."item_id"
        WHERE item."char_id" = p_source_char_id
          AND item."item_type" NOT IN (
              2375, 3500, 3501, 3502, 4422, 4423, 4424, 4425,
              4442, 4443, 4444
          )
    LOOP
        INSERT INTO "world"."user_item" AS new_item (
            "char_id", "item_type", "amount", "enchant", "eroded",
            "bless", "ident", "wished", "warehouse", "variation_opt1",
            "variation_opt2", "intensive_item_type", "hlocked",
            "use_count"
        ) VALUES (
            v_new_char_id, v_source_item.item_type, v_source_item.amount,
            v_source_item.enchant, v_source_item.eroded,
            v_source_item.bless, v_source_item.ident, v_source_item.wished,
            v_source_item.warehouse, v_source_item.variation_opt1,
            v_source_item.variation_opt2,
            v_source_item.intensive_item_type, v_source_item.hlocked,
            v_source_item.use_count
        )
        RETURNING new_item."item_id" INTO v_new_item_id;

        IF v_source_item.attack_attribute_type <> -1 THEN
            INSERT INTO "world"."user_item_attribute" (
                "item_id", "attack_attribute_type",
                "attack_attribute_value", "defend_attribute_0",
                "defend_attribute_1", "defend_attribute_2",
                "defend_attribute_3", "defend_attribute_4",
                "defend_attribute_5"
            ) VALUES (
                v_new_item_id, v_source_item.attack_attribute_type,
                v_source_item.attack_attribute_value,
                v_source_item.defend_attribute_0,
                v_source_item.defend_attribute_1,
                v_source_item.defend_attribute_2,
                v_source_item.defend_attribute_3,
                v_source_item.defend_attribute_4,
                v_source_item.defend_attribute_5
            );
        END IF;

        IF v_source_item.duration > -1 THEN
            INSERT INTO "world"."user_item_duration" (
                "item_id", "duration"
            ) VALUES (v_new_item_id, v_source_item.duration);
        END IF;

        IF v_source_item.start_time > -1 THEN
            INSERT INTO "world"."user_item_period" (
                "item_id", "start_time", "period", "item_type"
            ) VALUES (
                v_new_item_id, v_source_item.start_time,
                v_source_item.period, v_source_item.item_type
            );
        END IF;
    END LOOP;

    INSERT INTO "world"."user_history" (
        "char_name", "char_id", "log_date", "log_action",
        "account_name", "create_date"
    ) VALUES (
        v_new_char_name, v_new_char_id, "world"."mssql_getdate"(), 1,
        p_account_name, NULL
    );

    IF v_reserved_name IS NOT NULL THEN
        UPDATE "world"."user_name_reserved" AS reservation
        SET "used" = 1
        WHERE "world"."mssql_text_eq"(
            reservation."char_name",
            v_reserved_name
        );
    END IF;

    RETURN QUERY SELECT v_new_char_id;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_restorechar2"(
    p_account_id integer,
    p_char_id integer,
    p_new_char_name text,
    p_account_name text DEFAULT ''
) RETURNS TABLE (
    item_id integer,
    item_type integer,
    amount bigint,
    enchant integer,
    eroded integer,
    bless integer,
    ident integer,
    wished integer,
    warehouse integer,
    variation_opt1 integer,
    variation_opt2 integer,
    intensive_item_type integer,
    hlocked smallint,
    use_count smallint,
    attack_attribute_type smallint,
    attack_attribute_value smallint,
    defend_attribute_0 smallint,
    defend_attribute_1 smallint,
    defend_attribute_2 smallint,
    defend_attribute_3 smallint,
    defend_attribute_4 smallint,
    defend_attribute_5 smallint,
    duration integer
)
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_data" AS character
    SET "account_id" = p_account_id,
        "char_name" = p_new_char_name,
        "account_name" = CASE
            WHEN length(p_account_name) > 0 THEN p_account_name
            ELSE character."account_name"
        END,
        "temp_delete_date" = NULL
    WHERE character."char_id" = p_char_id;

    IF NOT EXISTS (
        SELECT 1
        FROM "world"."user_item" AS current_item
        WHERE current_item."char_id" = p_char_id
    ) THEN
        RETURN QUERY
        SELECT deleted."item_id", deleted."item_type", deleted."amount",
               deleted."enchant", deleted."eroded",
               deleted."bless"::integer, deleted."ident",
               deleted."wished"::integer, deleted."warehouse",
               deleted."variation_opt1", deleted."variation_opt2",
               deleted."intensive_item_type", deleted."hlocked",
               deleted."use_count",
               COALESCE(attribute."attack_attribute_type", -2)::smallint,
               COALESCE(attribute."attack_attribute_value", 0)::smallint,
               COALESCE(attribute."defend_attribute_0", 0)::smallint,
               COALESCE(attribute."defend_attribute_1", 0)::smallint,
               COALESCE(attribute."defend_attribute_2", 0)::smallint,
               COALESCE(attribute."defend_attribute_3", 0)::smallint,
               COALESCE(attribute."defend_attribute_4", 0)::smallint,
               COALESCE(attribute."defend_attribute_5", 0)::smallint,
               COALESCE(item_duration."duration", -1)
        FROM "world"."user_item_deleted" AS deleted
        LEFT JOIN "world"."user_item_attribute" AS attribute
          ON attribute."item_id" = deleted."item_id"
        LEFT JOIN "world"."user_item_duration" AS item_duration
          ON item_duration."item_id" = deleted."item_id"
        WHERE deleted."char_id" = p_char_id
          AND deleted."isrestored" = 0;

        UPDATE "world"."user_item_deleted" AS deleted
        SET "isrestored" = 1
        WHERE deleted."char_id" = p_char_id;
    END IF;
END;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_restorepet"(
    p_char_id integer,
    p_item_id integer,
    p_new_item_id integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    v_npc_class_id integer;
    v_experience bigint;
    v_hp double precision;
    v_mp double precision;
    v_meal integer;
    v_spell_point bigint;
BEGIN
    SELECT pet."npc_class_id", pet."expoint", pet."hp", pet."mp",
           pet."meal", pet."sp"
    INTO v_npc_class_id, v_experience, v_hp, v_mp, v_meal, v_spell_point
    FROM "world"."user_item_deleted" AS deleted
    LEFT JOIN "world"."pet_data" AS pet
      ON pet."pet_id" = deleted."item_id"
    WHERE deleted."char_id" = p_char_id
      AND deleted."item_id" = p_item_id;

    PERFORM "world"."lin_createpet"(
        p_new_item_id, v_npc_class_id, v_experience,
        v_hp, v_mp, v_meal, v_spell_point
    );
END;
$function$;

COMMIT;
