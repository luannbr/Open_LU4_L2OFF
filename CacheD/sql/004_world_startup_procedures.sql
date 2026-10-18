-- Startup procedures. The procedure signatures match Cached.exe. The bodies
-- are PostgreSQL versions of the original behavior.

BEGIN;

CREATE SCHEMA IF NOT EXISTS "world";

-- Platform boundary: MSSQL EncryptByPassPhrase is unavailable on
-- PostgreSQL. pgcrypto supplies authenticated portable-at-rest encryption.
-- its ciphertext is not byte-compatible with MSSQL.
CREATE EXTENSION IF NOT EXISTS pgcrypto WITH SCHEMA "world";

-- The current one-time quest list lives in external production SQL and
-- was not present in any recovered database export. This explicit carrier
-- is a PostgreSQL deployment input, not a reconstructed target table.
CREATE TABLE IF NOT EXISTS "world"."cached_portable_one_time_quest_db_job" (
    job_version integer NOT NULL,
    quest_id integer NOT NULL,
    CONSTRAINT "pk_cached_portable_one_time_quest_db_job"
        PRIMARY KEY (job_version, quest_id)
);

COMMENT ON TABLE "world"."cached_portable_one_time_quest_db_job" IS
    'Portable deployment carrier for the unavailable current lin_LoadOneTimeQuestListForDBJob body';

-- Private PostgreSQL helper for the only lin_DeleteItemByType call made
-- by the recovered DB-job path. Persistence effects come from three
-- byte-identical modern donor bodies. Hlocked/use_count are the required
-- current-schema adaptation. Empty title/content make notification mail
-- unreachable for this caller and it is absent here.
CREATE OR REPLACE FUNCTION "world"."cached_portable_delete_item_by_type_for_dbjob"(
    p_item_type integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    CREATE TEMP TABLE IF NOT EXISTS cached_dbjob_item_in_post (
        item_id integer,
        char_id integer,
        item_type integer,
        amount bigint,
        stackable boolean,
        post_id integer
    ) ON COMMIT DROP;
    TRUNCATE TABLE pg_temp.cached_dbjob_item_in_post;

    INSERT INTO pg_temp.cached_dbjob_item_in_post (
        item_id, char_id, item_type, amount, stackable, post_id
    )
    SELECT linked.item_id, linked.postkeeper_id, attached.item_type,
           attached.amount, metadata.is_stackable, linked.post_id
    FROM "world"."post_item_admin" AS linked
    JOIN "world"."user_item" AS attached ON attached.item_id = linked.item_id
    JOIN "world"."itemdata" AS metadata ON metadata.item_type = attached.item_type
    WHERE linked.post_id IN (
        SELECT DISTINCT candidate_link.post_id
        FROM "world"."user_item" AS candidate
        JOIN "world"."post_item_admin" AS candidate_link
          ON candidate_link.item_id = candidate.item_id
        JOIN "world"."post" AS candidate_post
          ON candidate_post.post_id = candidate_link.post_id
        WHERE candidate_post.process < 3
          AND candidate.item_type = p_item_type
    );

    UPDATE "world"."user_item" AS moved
    SET warehouse = 1
    WHERE moved.item_id IN (
        SELECT item_id FROM pg_temp.cached_dbjob_item_in_post
        WHERE NOT stackable
    );

    UPDATE "world"."user_item" AS stored
    SET amount = stored.amount + attached.amount
    FROM pg_temp.cached_dbjob_item_in_post AS attached
    WHERE stored.char_id = attached.char_id
      AND stored.item_type = attached.item_type
      AND stored.warehouse = 1
      AND attached.stackable;

    INSERT INTO "world"."user_item" (
        char_id, item_type, amount, enchant, eroded, bless, ident,
        wished, warehouse, variation_opt1, variation_opt2,
        intensive_item_type, inventory_slot_index, hlocked, use_count
    )
    SELECT attached.char_id, attached.item_type, attached.amount,
           0, 0, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0
    FROM pg_temp.cached_dbjob_item_in_post AS attached
    WHERE attached.stackable
      AND NOT EXISTS (
          SELECT 1 FROM "world"."user_item" AS stored
          WHERE stored.char_id = attached.char_id
            AND stored.item_type = attached.item_type
            AND stored.warehouse = 1
      );

    DELETE FROM "world"."user_item"
    WHERE item_id IN (
        SELECT item_id FROM pg_temp.cached_dbjob_item_in_post
        WHERE stackable
    );

    DELETE FROM "world"."post"
    WHERE post_id IN (
        SELECT post_id FROM pg_temp.cached_dbjob_item_in_post
    );
    DELETE FROM "world"."post_item_admin"
    WHERE post_id IN (
        SELECT post_id FROM pg_temp.cached_dbjob_item_in_post
    );

    UPDATE "world"."commission"
    SET item_delete_time = "world"."mssql_getdate"()
    WHERE item_classid = p_item_type;

    CREATE TEMP TABLE IF NOT EXISTS cached_dbjob_warehouse (
        char_id integer NOT NULL,
        warehouse integer NOT NULL,
        PRIMARY KEY (char_id, warehouse)
    ) ON COMMIT DROP;
    TRUNCATE TABLE pg_temp.cached_dbjob_warehouse;

    INSERT INTO pg_temp.cached_dbjob_warehouse (char_id, warehouse)
    SELECT DISTINCT char_id, warehouse FROM "world"."user_item"
    WHERE item_type = p_item_type
    ON CONFLICT DO NOTHING;
    INSERT INTO pg_temp.cached_dbjob_warehouse (char_id, warehouse)
    SELECT DISTINCT char_id, 1 FROM pg_temp.cached_dbjob_item_in_post
    ON CONFLICT DO NOTHING;

    DELETE FROM "world"."user_item" WHERE item_type = p_item_type;
    DELETE FROM "world"."user_item_deleted" WHERE item_type = p_item_type;
    UPDATE "world"."user_premium_item"
    SET item_remain = 0
    WHERE item_id = p_item_type;

    DELETE FROM "world"."item_check" AS checksum
    USING pg_temp.cached_dbjob_warehouse AS affected
    WHERE checksum.owner = affected.char_id
      AND checksum.warehouse = affected.warehouse;

    INSERT INTO "world"."item_check" (owner, warehouse, checksum)
    SELECT remaining.char_id, remaining.warehouse,
           "world".pgp_sym_encrypt(
               SUM(remaining.item_id::bigint + remaining.item_type::bigint +
                   remaining.amount + remaining.enchant::bigint)::text,
               '123456789!',
               'cipher-algo=aes256, compress-algo=0'
           )
    FROM "world"."user_item" AS remaining
    JOIN pg_temp.cached_dbjob_warehouse AS affected
      ON affected.char_id = remaining.char_id
     AND affected.warehouse = remaining.warehouse
    GROUP BY remaining.char_id, remaining.warehouse;
END;
$function$;

-- Both compatible modern exports keep the target-era empty body.
CREATE OR REPLACE FUNCTION "world"."lin_cleanupdeletedpost"()
RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    NULL;
END;
$function$;

-- Current ABI is exact. Body is shared by Classic 3.0 and Ertheia donors.
CREATE OR REPLACE FUNCTION "world"."lin_cleanupghostdata"()
RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    UPDATE "world"."user_data"
    SET pledge_id = 0, nickname = ''
    WHERE account_id IN (-1, -3, -4) AND pledge_id > 0;

    DELETE FROM "world"."user_friend"
    WHERE char_id IN (SELECT char_id FROM "world"."user_data" WHERE account_id IN (-1, -3, -4))
       OR friend_char_id IN (SELECT char_id FROM "world"."user_data" WHERE account_id IN (-1, -3, -4));

    DELETE FROM "world"."user_blocklist"
    WHERE char_id IN (SELECT char_id FROM "world"."user_data" WHERE account_id IN (-1, -3, -4))
       OR block_char_id IN (SELECT char_id FROM "world"."user_data" WHERE account_id IN (-1, -3, -4));

    DELETE FROM "world"."pledge"
    WHERE ruler_id IN (SELECT char_id FROM "world"."user_data" WHERE account_id IN (-1, -3, -4));

    DELETE FROM "world"."pledge"
    WHERE pledge_id NOT IN (SELECT DISTINCT pledge_id FROM "world"."user_data");

    UPDATE "world"."user_data"
    SET pledge_id = 0
    WHERE pledge_id > 0 AND pledge_id NOT IN (SELECT pledge_id FROM "world"."pledge");

    UPDATE "world"."agit"
    SET pledge_id = 0
    WHERE pledge_id > 0 AND pledge_id NOT IN (SELECT pledge_id FROM "world"."pledge");

    UPDATE "world"."castle"
    SET pledge_id = 0
    WHERE pledge_id > 0 AND pledge_id NOT IN (SELECT pledge_id FROM "world"."pledge");

    DELETE FROM "world"."war_declare"
    WHERE (challengee > 0 AND challengee NOT IN (SELECT pledge_id FROM "world"."pledge"))
       OR (challenger > 0 AND challenger NOT IN (SELECT pledge_id FROM "world"."pledge"));

    DELETE FROM "world"."siege_agit_pledge"
    WHERE pledge_id > 0 AND pledge_id NOT IN (SELECT pledge_id FROM "world"."pledge");

    DELETE FROM "world"."team_battle_agit_pledge"
    WHERE pledge_id > 0 AND pledge_id NOT IN (SELECT pledge_id FROM "world"."pledge");

    DELETE FROM "world"."alliance"
    WHERE master_pledge_id NOT IN (SELECT pledge_id FROM "world"."pledge");

    UPDATE "world"."pledge"
    SET alliance_id = 0
    WHERE alliance_id > 0 AND alliance_id NOT IN (SELECT id FROM "world"."alliance");

    INSERT INTO "world"."commission_abnormal" ("commission_id", "char_id", "buyer_id", "item_name", "item_price", "item_commission_type", "item_commission_first_type", "commission_fee", "item_commission_expired_type", "item_expired_time", "char_name", "item_id", "item_grade", "item_general", "item_register_time", "item_delete_time", "item_classid", "discount_type")
    SELECT "commission_id", "char_id", "buyer_id", "item_name", "item_price", "item_commission_type", "item_commission_first_type", "commission_fee", "item_commission_expired_type", "item_expired_time", "char_name", "item_id", "item_grade", "item_general", "item_register_time", "item_delete_time", "item_classid", "discount_type" FROM "world"."commission"
    WHERE item_id NOT IN (SELECT item_id FROM "world"."user_item")
      AND item_delete_time IS NULL AND buyer_id = 0;

    DELETE FROM "world"."commission"
    WHERE item_id NOT IN (SELECT item_id FROM "world"."user_item")
      AND item_delete_time IS NULL AND buyer_id = 0;
END;
$function$;

-- Current caller fetches one integer. Both compatible donors return zero.
CREATE OR REPLACE FUNCTION "world"."lin_deleteoldpost"()
RETURNS TABLE(deleted_count integer)
LANGUAGE sql
VOLATILE
AS $function$
    SELECT 0::integer;
$function$;

CREATE OR REPLACE FUNCTION "world"."lin_getdbversion"()
RETURNS TABLE(db_version text)
LANGUAGE sql
STABLE
AS $function$
    SELECT 'L2_WORK_2018_05_15_DAILYBUILD_05_00'::text;
$function$;

-- Exact current owner calls this before repopulating every row.
CREATE OR REPLACE FUNCTION "world"."lin_inititemdata"()
RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    TRUNCATE TABLE "world"."itemdata";
END;
$function$;

-- Current can_move is a 0..2 integer. Donor BIT declarations are stale.
CREATE OR REPLACE FUNCTION "world"."lin_insertitemdata"(
    p_item_type integer,
    p_can_move integer,
    p_is_stackable boolean,
    p_is_period boolean,
    p_is_quest_item boolean
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."itemdata" (
        item_type, can_move, is_stackable, is_period, is_quest_item
    ) VALUES (
        p_item_type, p_can_move, p_is_stackable, p_is_period, p_is_quest_item
    );
$function$;

-- Current EXE binds expiry_time as the third result. Donor SELECTs omit it.
CREATE OR REPLACE FUNCTION "world"."lin_loadpledgeskill"(
    p_pledge_id integer
) RETURNS TABLE (skill_id integer, skill_lev integer, expiry_time integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT ps.skill_id, ps.skill_lev::integer, ps.expiry_time
    FROM "world"."pledge_skill" AS ps
    WHERE ps.pledge_id = p_pledge_id;
$function$;

-- The fourth current argument is an integral long carried through SQL_DOUBLE.
CREATE OR REPLACE FUNCTION "world"."lin_acquirepledgeskill"(
    p_pledge_id integer,
    p_skill_id integer,
    p_skill_lev integer,
    p_expiry_time double precision
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    INSERT INTO "world"."pledge_skill" (
        pledge_id, skill_id, skill_lev, expiry_time
    ) VALUES (
        p_pledge_id, p_skill_id, p_skill_lev::smallint,
        p_expiry_time::integer
    )
    ON CONFLICT (pledge_id, skill_id) DO UPDATE
    SET skill_lev = EXCLUDED.skill_lev,
        expiry_time = EXCLUDED.expiry_time;
$function$;

-- Current ABI is exact. Body is shared by Classic 3.0 and Ertheia donors.
CREATE OR REPLACE FUNCTION "world"."lin_movecommission"()
RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
BEGIN
    INSERT INTO "world"."commission_history" ("commission_id", "char_id", "buyer_id", "item_name", "item_price", "item_commission_type", "item_commission_first_type", "commission_fee", "item_commission_expired_type", "item_expired_time", "char_name", "item_id", "item_grade", "item_general", "item_register_time", "item_delete_time", "item_classid", "discount_type")
    SELECT "commission_id", "char_id", "buyer_id", "item_name", "item_price", "item_commission_type", "item_commission_first_type", "commission_fee", "item_commission_expired_type", "item_expired_time", "char_name", "item_id", "item_grade", "item_general", "item_register_time", "item_delete_time", "item_classid", "discount_type" FROM "world"."commission"
    WHERE NOT (buyer_id = 0 AND item_delete_time IS NULL);

    DELETE FROM "world"."commission"
    WHERE NOT (buyer_id = 0 AND item_delete_time IS NULL);
END;
$function$;

-- Exact current 0->(integer,binary[256]) fetch ABI. The direct
-- projection is a PostgreSQL compatibility body because current T-SQL
-- is external to the executable.
CREATE OR REPLACE FUNCTION "world"."lin_loadallonetimequest"()
RETURNS TABLE (char_id integer, quest_flag bytea)
LANGUAGE sql
STABLE
AS $function$
    SELECT ud.char_id, ud.quest_flag
    FROM "world"."user_data" AS ud;
$function$;

-- Exact current binary[256],integer -> void ABI.
CREATE OR REPLACE FUNCTION "world"."lin_updateonetimequest"(
    p_quest_flag bytea,
    p_char_id integer
) RETURNS void
LANGUAGE sql
VOLATILE
AS $function$
    UPDATE "world"."user_data"
    SET quest_flag = p_quest_flag
    WHERE char_id = p_char_id;
$function$;

-- Exact current 0->integer ABI. The version-3 list itself is missing
-- from all recovered SQL. Deployment must populate the explicitly
-- classified portable carrier. The (0,6,3) gate is inferred from the
-- machine-confirmed marker written by CDBJobDeleteQuest::DoInternal.
CREATE OR REPLACE FUNCTION "world"."lin_loadonetimequestlistfordbjob"()
RETURNS TABLE (quest_id integer)
LANGUAGE sql
STABLE
AS $function$
    SELECT configured.quest_id
    FROM "world"."cached_portable_one_time_quest_db_job" AS configured
    WHERE configured.job_version = 3
      AND NOT EXISTS (
          SELECT 1 FROM "world"."regular_db_job" AS completed
          WHERE completed.booking_time = 0
            AND completed.job_type = 6
            AND completed.job_value = 3
      )
    ORDER BY configured.quest_id;
$function$;

-- Exact current integer,integer,integer -> void ABI. Types 1..4 are
-- pinned to three byte-identical donor bodies. Type 6 is confirmed by
-- current machine code as the one-time-quest completion marker.
CREATE OR REPLACE FUNCTION "world"."lin_executedbjob"(
    p_booking_time integer,
    p_job_type integer,
    p_job_value integer
) RETURNS void
LANGUAGE plpgsql
VOLATILE
AS $function$
DECLARE
    claimed_rows integer := 0;
    normalized_quest_id integer := p_job_value;
BEGIN
    IF p_job_type NOT IN (1, 2, 3, 4, 6) THEN
        RETURN;
    END IF;

    -- PostgreSQL concurrency replacement for donor IF NOT EXISTS + INSERT.
    -- Claim and effects share this function transaction, so an error rolls
    -- the marker back with the mutations.
    INSERT INTO "world"."regular_db_job" (
        booking_time, job_type, job_value, execute_time
    ) VALUES (
        p_booking_time, p_job_type, p_job_value, "world"."mssql_getdate"()
    ) ON CONFLICT (booking_time, job_type, job_value) DO NOTHING;
    GET DIAGNOSTICS claimed_rows = ROW_COUNT;
    IF claimed_rows = 0 THEN
        RETURN;
    END IF;

    IF p_job_type = 1 THEN
        PERFORM "world"."cached_portable_delete_item_by_type_for_dbjob"(p_job_value);
    ELSIF p_job_type = 2 THEN
        DELETE FROM "world"."user_newactiveskill"
        WHERE skill_id = p_job_value;
    ELSIF p_job_type = 3 THEN
        DELETE FROM "world"."user_quest"
        WHERE quest_no = p_job_value;

        IF normalized_quest_id > 10000 THEN
            normalized_quest_id := normalized_quest_id - 10000;
        END IF;
        IF (p_job_value <= 255 OR p_job_value > 10000)
           AND normalized_quest_id BETWEEN 0 AND 2047 THEN
            UPDATE "world"."user_data"
            SET quest_flag = set_byte(
                quest_flag,
                normalized_quest_id / 8,
                get_byte(quest_flag, normalized_quest_id / 8)
                    & ~(1 << (normalized_quest_id % 8))
            )
            WHERE quest_flag IS NOT NULL
              AND octet_length(quest_flag) = 256;
        END IF;
    ELSIF p_job_type = 4 THEN
        DELETE FROM "world"."ui_event_count"
        WHERE log_date < p_job_value;
    END IF;
END;
$function$;

COMMIT;
