<?php
/**
 * @file
 * IT Pro migration classes.
 *
 * Migration classes for importing legacy Auto Express content into the Auto Express
 * Distro bases site.
 *
 * VERY IMPORTANT! - preserve any files created by adding a migrate file usage entry
 * example: file_usage_add($file, 'migrate', 'file', $file->fid);
 *
 */

module_load_include('inc', 'magazine_migrate', 'magazine_migrate_bbcode');
module_load_include('inc', 'magazine_migrate', 'magazine_migrate_channels');

/**
 * Abstract base class holding common settings for article migration.
 *
 * @see migrate.drush.inc for migrate drush commands
 */
abstract class ITProArticleBaseMigration extends DennisMigrateToolsBase {

  protected $subsection_ids = array (
    'News' => 39,
    'How tos' => 42,
    'Reviews' => 45,
    'Labs' => 48,
    'Features' => 51,
    'Research' => 71,
    'Blog' => 73,
  );

  protected $subsection_aliases = array (
    'News' => 'News',
    'How tos' => 'Tutorial',
    'Reviews' => 'Review',
    'Labs' => 'Labs',
    'Features' => 'Analysis & Insight',
    'Research' => 'Research',
    'Blog' => 'Blog',
  );

  // Extra fields in subsection table that need to be normalised. Add to these
  // any additional fields that appear in tables with names like S_subsection_X
  public $section_fields = array(
    'lead_headline',
  );

  /**
   *
   *
   */
  public function __parentConstruct() {
    parent::__construct();
  }

  public function __construct() {
    parent::__construct(MigrateGroup::getInstance('itpro_articles'));

    // We instantiate the MigrateMap
    $this->map = new MigrateSQLMap($this->machineName,
      array(
        'id' => array(
          'type' => 'int',
          'not null' => TRUE,
          'description' => 'Article Id',
          'alias' => 'a',
        )
      ),
      MigrateDestinationNode::getKeySchema()
    );

    // The following Mappings are the base mapping common to all articles on IT Pro
    // Other fields to be mapped are stored in the individual S_subsection_"subsection id" table for a subsection
    // Therefore we do that bit of mapping in the individual subsection migration class.
    // We also add callbacks to the mapping that handle encoding and html cleanup issues.
    $this->addFieldMapping('title', 'r_headline')
      ->callbacks(array($this, 'cleanHtml'), array($this, 'cleanPlainText'))
      ->description(t('Mapping r_headline in source to field Title'));

    $this->addFieldMapping('pathauto')
      ->defaultValue(FALSE);

    $this->addFieldMapping('field_short_teaser', 'summary')
      ->callbacks(array($this, 'cleanHtml'), array($this, 'cleanPlainText'))
      ->description(t('Mapping summary in source to field_summary. If the summary in the source is blank we will check to see if there is data in the lead_headline instead'));

    $this->addFieldMapping('body', 'body_copy')
      ->callbacks(array($this, 'cleanHtml'), array($this, 'stripFacebook'), array($this, 'itProBbToHtml'))
      ->arguments(array('format' => 'migration'))
      ->description(t('Mapping body_copy in source to field Body'));

    $this->addFieldMapping('field_author', 'text_byline')
      ->description(t('Mapping of text_byline in handled in prepareRow()'));

    $this->addFieldMapping('field_short_title', 'link_headline')
      ->callbacks(array($this, 'cleanHtml'))
      ->description(t('Mapping of link_headline in source to short_title'));

    // Valid from - published date
    $this->addFieldMapping('created', 'valid_from')
      ->description(t('Mapping valid_from in source to node created date. If valid_from is in the future prepare() also maps this to field_published_date'));

    // Article display date - published date
    $this->addFieldMapping('field_published_date')
      ->description(t('Mapping of field_published_date. This has valid_from mapped to it if valid_from is in the future. Otherwise article_display_date. This is handled in prepare()'));
      
    // Channels are migrated to secondary_category
    //$this->addFieldMapping('secondary_category', 'channels')
    //  ->description(t('Mapping of secondary_cateogry. This is mapped to a comma separated list of channel ids which need to be migrated to separate values on the same field.'));
  }

  /**
   * Converts any IT Pro BB syntax we find in the body copy to HTML
   *
   */
  public function itProBbToHtml($string) {
    $string = magazine_migrate_bbcode_to_html($string);
    return $string;
  }

  /**
   * Cleans the html in strings
   *
   * Ensures characters are UTF-8
   * Replaces line breaks in text
   * @param string $string
   */
  public function cleanExtendedHtml($string) {
    settype($string, 'string');

    $string = str_replace("â€™", "'", $string);
    $string = str_replace("â€“", "-", $string);
    $string = str_replace("â€¦", ".", $string);
    $string = str_replace("â€˜", "'", $string);
    $string = str_replace("â€œ", "'", $string);
    $string = str_replace("â€", "'", $string);

    // some incoming characters are messed up so this cleans them
    $encoding = mb_detect_encoding($string);
    $string = iconv($encoding, 'UTF-8//TRANSLIT', $string);

    // Look for and fix embedded images
    // All images in IT Pro should be in BBcode, so this probably needn't do
    // anything
    $string = preg_replace_callback(
      '|<img(.*?)>|si',
      array($this, 'bodyImageCallback'),
      $string
    );

    return $string;
  }


  /**
   * Callback for the preg_replace in $this->cleanHtml()
   *
   * @param array $parseMatches
   */
  public function bodyImageCallback($parseMatches) {
    // look for embedded autoexpress images
    $pos = strpos($parseMatches[1], 'front_picture_library_UK');
    if ($pos !== FALSE) {
      // found an embedded autoexpress image
      // import it and use the media embed code
      if (preg_match('|car_photo_(.*?)_(.*?).jpg|', $parseMatches[1], $matches)) {
        $filename = 'car_photo_' . $matches[1] . '.jpg';
        $image = $this->importImage($filename, NULL);
        if (!empty($image['fid'])) {
          $fid = $image['fid'];
          return '[[{"type":"media","view_mode":"content_half_width","fid":"'
          . $fid . '","attributes":{"alt":"","class":"media-image","height":"201","width":"300"}}]]';
        }
      }
    }
  }

  /**
  * Called before any mapping is done
  * @param unknown_type $row
  */
  public function prepareRow($row) {
    // get the helper
    $handle = ITProMigrationHelper::getInstance();

    // Normalise sub section fields
    // - should be called before any functions working on sub section fields
    // Where we have a field coming out of a different joined table depending on
    // the article type, it will natuarally show up as different fields in the
    // $row object. We need to have a single field
    $this->normaliseSubSectionFields($row);

    // Get the author node reference nid
    $author_byline = (string) $row->text_byline;
    $ids = $handle->getAuthorReferenceNidFromByline($author_byline);

    if (!empty($ids)) {
      $row->text_byline = $ids[0]; // Set the first author name

      // Add email
      if (!empty($row->author_email)) {
        if (!empty($ids)) {

          // Store the authors that have already log a message
          $logged_authors_error_1 = variable_get("magazine_migrate_logged_authors_error_1", array());
          $logged_authors_error_2 = variable_get("magazine_migrate_logged_authors_error_2", array());

          foreach ($ids as $author_nid) {
            $result = $handle->setAuthorEmail($author_nid, $row->author_email);
            if ($result == -1) {
              // If the email was already added to another author, show a message and log the email
              if (!in_array($author_nid, $logged_authors_error_1)) {
                $logged_authors_error_1[]= $author_nid;
                variable_set("magazine_migrate_logged_authors_error_1", $logged_authors_error_1);
                //$watchdog_type = WATCHDOG_WARNING;
                $watchdog_type = WATCHDOG_INFO;
                watchdog('migrate', ' // Email ' . $row->author_email . ' already exists. (row id: ' . $row->id . '; node id: ' . $author_nid . ')', NULL, $watchdog_type);
              }

              // count number of repeated emails
              //$sql = 'SELECT author_email, count(*) from A_articles GROUP BY author_email ORDER BY count(*)  DESC';

            }
            else if ($result == -2) {
              if (!in_array($author_nid, $logged_authors_error_2)) {
                $logged_authors_error_2[]= $author_nid;
                variable_set("magazine_migrate_logged_authors_error_2", $logged_authors_error_2);
                $watchdog_type = WATCHDOG_INFO;
                watchdog('migrate', 'The author already has a different email address assigned. // (row id: ' . $row->id . '; node id: ' . $author_nid . ')', NULL, $watchdog_type);
              }
            }
          }
        }
      }


      unset($ids[0]);
      $row->additional_authors = $ids; // Save the rest to add to the field later
    }
    else {
      $row->text_byline = NULL;
    }
    $row->original_byline =  $author_byline;

    $row->link_headline = $this->cleanHeadlineText($row->link_headline, $row->id);
    $this->validateCreatedDate($row);
    $this->validatePublishedDate($row);

    // Use a different source field for the destination summary if the source
    // summary if blank
    $row->summary = trim($row->summary);
    if (empty($row->summary) && isset($row->lead_headline)) {
      $row->summary = trim($row->lead_headline);
    }
  }

  /**
   * Sets various values before the node is inserted
   *
   * Sets the published date- is a datetime field (2002-03-01 15:58:10)
   * Sets the main image
   *
   * @param unknown_type $node
   * @param unknown_type $row
   */
  public function prepare(stdClass $node, stdClass $row) {

     // Set the main image
    $this->setMainImage($node, $row);

    //parent::prepare($node, $row);
    // Set the tag tid to dynamically create tag
    if ($row->channel != '') {
      // Channel taxonomy terms may also have a parent
      if ($tid = $this->createTaxonomyTerm($row->channel, 'category', $row->cms_channel_parent_channel)) {
        $node->field_category_primary[LANGUAGE_NONE][0]['tid'] = $tid;
      }
    }
    // Set the tag tid to dynamically create tag
    if ($row->subsection != '') {
      // Using the alias of the subsection, not the subsection itself.
      if ($tid = $this->createTaxonomyTerm($this->subsection_aliases[$row->subsection], 'article_type')) {
        $node->field_article_type[LANGUAGE_NONE][0]['tid'] = $tid;
      }
    }
    // Some nodes have additional authors. Set them here.
    if (!empty($row->additional_authors)) {
      foreach ($row->additional_authors as $i => $authors) {
        $node->field_author[LANGUAGE_NONE][$i]['nid'] = $authors;
      }
    }

    // Check to see if valid_from is in the future
    if (strtotime($row->valid_from) > strtotime(date("Y-m-d H:i:s"))) {
      // If valid_from is in the future then we need to set the
      // field_published_date to valid_from
      $this->setPublishedDate($node, $row->valid_from);
      // and check the schedule checkbox
      $node->field_schedule_publish[LANGUAGE_NONE][0]['value'] = 1;
    }
    else {
      // If the published date is NOT in the future we need to use the
      // article_display_date (which is also the node creation date) as the
      // field_published_date
      $this->setPublishedDate($node, $row->article_display_date);
      // and make sure the schedule checkbox is not checked
      $node->field_schedule_publish[LANGUAGE_NONE][0]['value'] = 0;
    }

    // If there is no gallery for this article, unset the empty field value.
    if ((isset($node->field_gallery[LANGUAGE_NONE][0]['nid'])) && (empty($node->field_gallery[LANGUAGE_NONE][0]['nid']))) {
      unset($node->field_gallery);
    }

    $node_title = $this->refineurl($node);
    $source_id = $row->id;
    // I think it's the path_save function in the complete method that actually
    // does the redirect but the alias does not appear in the node edit screen
    // unless we do this too.
    $node->path['alias'] = $source_id . "/" . $node_title;
    
    // We have a channels property in the $row object that contains a comma
    // separated list of the secondary channel ids.
    $this->setSecondaryChannels($node, $row->channels);
  }

  /**
   * Called after the node has been saved
   * @param unknown_type $entity
   * @param unknown_type $row
   */
  public function complete(stdClass $entity, stdClass $row) {
    /*
    if (!empty($entity->nid)) {
      // Store the original id in the migration mapping table
      $old_url = $this->getOldUri($row);
      if ($old_url){
        $this->updateRedirect($old_url, $entity->nid);
      }
    }
    */

    // Set meta data
    $this->setNodeMetaData($entity, $row);

    // get the helper
    $handle = ITProMigrationHelper::getInstance();

    // Some nodes may have really long bylines that are difficult to parse. Flag these up
    // $handle->flagVeryDirtyByLine($row->original_byline, $entity->nid);
    
    if (!empty($entity->nid)) {
      //It looks as though the easiest way to set the path alias is with this:
      $alias = array (
        'source' => 'node/'.$entity->nid,
        'alias' => $row->id.'/'.$this->refineurl($entity)
      );
      path_save($alias);
    }
  }


  /**
   * Refined the node title so that it can be used in the the url alias
   * @param unknown $node
   */
  public function refineurl(stdClass $node) {
    // trim and lower case the headline
    $node_title = $node->title;
    $headline   = trim(strtolower($node_title));
    // decode entities and strip all tags
    $headline   = strip_tags(html_entity_decode($headline));
    $headline   = preg_replace('/\s+/', '-', $headline);
    $str=preg_replace('/[^a-z0-9\-]/', '', $headline);
    $node_title = str_replace(' ', '-', strtolower($str));
    return $node_title;
  }

  /**
   * Set node meta data
   *
   * @param stdClass $entity
   * @param stdClass $row
   */

  public function setNodeMetaData(stdClass $entity, stdClass $row) {
    // Save meta data from A_articles table
    $parsed_title = explode('|', $row->webpage_title);

    if (isset($parsed_title[1])) {
      try {
        watchdog('migrate', "{$row->webpage_title} /// webpage_title for source row {$row->id} may not have been parsed correctly. Please check manually.");
      }
      catch(Exception $e) {

      }
      $row->webpage_title = $parsed_title[0];
    }

    $metatags = array();
    $row->webpage_title = trim($row->webpage_title);
    if (!empty($row->webpage_title)) {
      $row->webpage_title = $row->webpage_title.' | '.variable_get('site_name');
      $metatags['title'] = array('value' => $row->webpage_title);
    }

    $metatags['description'] = array('value' => $row->meta_tags_description);
    $metatags['keywords'] = array('value' => $row->meta_tags_keywords);
    $metatags['canonical'] = array('value' => trim($row->canonical_link));

    metatag_metatags_save('node', $entity->nid, $metatags);
  }

  /**
   * Called after the node has been rolled back
   *
   * Removes the mapping of legacy to new url from the redirects table
   * @param unknown_type $entity_id
   */
  public function completeRollback($entity_id) {
    //if (!empty($entity_id)) {
    //  $path = 'node/' . $entity_id;
    //  redirect_delete_by_path($path);
    //}
  }

  /**
   * Sets the created date to the last_changed date if the valid_from
   * date is '0000-00-00 00:00:00'
   * @param unknown_type $node
   * @param unknown_type $value
   */
  public function validateCreatedDate($row) {
    if ($row->valid_from == '0000-00-00 00:00:00') {
      $row->valid_from = $row->last_changed;
      // FIXME: Watchdog suffers from a weird problem when implemented in
      // prepareRow. The first call is printed to the screen and only subsequent
      // ones make it to the log
      watchdog('migrate', '%last_changed /// last_changed date used instead of valid_from on row %row because valid_from is 0',
        array(
          '%last_changed' => $row->last_changed,
          '%row' => $row->id,
        )
      );
    }
  }

  /**
   * Sets the published date to the last_changed date if the article_display_date
   * date is '0000-00-00 00:00:00'
   * @param unknown_type $node
   * @param unknown_type $value
   */
  public function validatePublishedDate($row) {
    if ($row->article_display_date == '0000-00-00 00:00:00') {
      $row->article_display_date = $row->last_changed;
      watchdog('migrate', '%last_changed /// last_changed date used instead of article_display_date on row %row because article_display_date is 0',
        array(
          '%last_changed' => $row->last_changed,
          '%row' => $row->id,
        )
      );
    }
  }

  /**
   * If link_headline is not set use lead_headline or r_headline
   *
   * @param $row
   */
  /*
  public function validateLinkHeadline($row) {
    if ((!isset($row->link_headline)) || empty($row->link_headline)) {
      if ((isset($row->lead_headline)) || !empty($row->lead_headline)) {
        $row->link_headline = $row->lead_headline;
      }
      else {
        $row->link_headline = $row->r_headline;
      }
    }
  }*/

  /**
   * Converts an IT Pro rating to a percentage that can be used for a 5 star
   * field
   *
   * @param unknown_type $value
   */
  public function fiveStarPercentage ($value) {
    if (is_numeric($value)) {
      // If the score is greater than 1 take 1 off
      if ($value > 1) {
        $value--;
      }
    }
    else {
      watchdog('migrate',"{$value} /// rating is not an integer between 1 and 6. Please check manually.");
      return 0;
    }
    return floatval($value) * 20;
  }

  /**
   * Formats year as a MySQL timestamp
   *
   * @param unknown_type $year
   */
  public function formatYearTimestamp($year) {
    return intval($year) . '-01-01 00:00:00';
  }

  public function cleanPlainText($str) {
    $str = strip_tags($str);
    return html_entity_decode($str, ENT_COMPAT | ENT_QUOTES, 'UTF-8');
  }

  /**
   * Return number with 'st', 'nd', 'rd', 'th' suffix
   * @param $int
   */
  public function ordinalNumberFormat($int) {
    // change increment variable to ordinal number.
    $n1 = $int % 100; //first remove all but the last two digits
    $n2 = ($n1 < 20 ? $int : $int % 10); //remove all but last digit unless the number is in the teens, which all should be 'th'
    //$n is now used to determine the suffix.
    $ord = ($n2==1 ? $int.'st' : ( ($n2==2 ? $int.'nd' : ($n2==3 ? $int.'rd' : $int.'th') ) ) );
    return $ord;
  }

  /**
   * Returns base query object for legacy Articles
   *
   * The query conditions should be implemented in the
   * specific migration class e.g.
   *
   * e.g. in autoExpressNewsArticlesMigration() after a call to this function is made
   * we do $query->condition(db_or()->condition('a.subsection_select', 40)->condition('a.subsection_select', 52));
   */
  public function getArticlesQuery(){

    $query = Database::getConnection('default', 'itpro')
      ->select('A_articles', 'a')
      ->fields('a', array('id', 'subsection_select', 'r_headline', 'summary', 'body_copy', 'active', 'author_email', 'text_byline', 'index_id',
        'parent_article', 'link_headline', 'webpage_title', 'meta_tags_description', 'meta_tags_keywords', 'valid_from',
        'article_display_date', 'last_changed', 'canonical_link', 'meta_tags_keywords'));
      // Add other fields we want to refer to to the line above

    // Use sub query to get main image
    // Note: use_for_article_main_image in an ENUM field and is sorted by the order values are declared (not alphabetically)
    //
    // Priority
    // - use for main image = 'yes'
    //   - Highest index_id
    //
    $sub_query = "(
        SELECT id
        FROM A_screenshots i
        WHERE
          i.r_article_id = a.id AND
          i.active = 'yes'
        ORDER BY
          i.use_for_article_main_image ASC,
          i.index_id DESC
        LIMIT 1
      ) ";

    $query->leftjoin('A_screenshots', 'images', "images.id = " . $sub_query);
    $query->fields('images', array('id', 'r_article_id', 'alt_tag', 'caption', 'use_for_article_main_image', 'active', 'picture_suffix'));

    // Add other fields we want to refer to to the line above
    // Sub section - Used to get directory name for redirect
    $query->leftjoin('CMS_channel', 'cms_channel', 'cms_channel.id = a.primary_channel');
    $query->fields('cms_channel', array('directory_name', 'channel'));  
    
    // Join on the parent term if this is present
    $query->leftjoin('CMS_channel', 'cms_channel_parent', 'cms_channel_parent.id = cms_channel.parent');
    $query->fields('cms_channel_parent', array('directory_name', 'channel'));  
    
    // Sub section - Used to get directory name for redirect
    $query->leftjoin('CMS_subsection', 'cms_subsection', 'cms_subsection.id = a.subsection_select');
    $query->fields('cms_subsection', array('directory_name', 'subsection'));
    
    // Channels - used to get the secondary categories
    $query->leftjoin('A_channel', 'ach', 'ach.R_article_id = a.id');
    // We're going to shove all channels for a particular row into a single comma
    // separated field. This prevents us having to do a separate query on each
    // row migrate
    $query->addExpression('GROUP_CONCAT(DISTINCT ach.channel_select)', 'channels');
    
    $query->fields('cms_channel', array('directory_name', 'channel'));

    // Sub section - Used to get directory name for redirect
    $query->leftjoin('CMS_subsection', 'cms_subsection', 'cms_subsection.id = a.subsection_select');
    $query->fields('cms_subsection', array('directory_name', 'subsection'));

    // Only fetch articles with a valid_to date beyond today
    $query->where('a.valid_to > now()');

    // Only fetch active articles
    $query->condition('a.active', 'yes');

    // We want to migrate the children before the parents
    $query->orderBy('a.parent_article', 'DESC');
    $query->groupBy('a.id');

    return $query;
  }

  /**
   * Override query order by
   *
   * @param $query
   * @param $newOrderBy
   */
  public function setQueryOrderBy($query, $newOrderBy) {
    $orderBy =& $query->getOrderBy();
    $orderBy = $newOrderBy;
  }

  /**
   * Strip any inline facebook share buttons
   *
   * @todo move to DennisMigrateToolsBase
   *
   * @param $string
   */
  public function stripFacebook ($string) {
    $pattern = "/\<div id=\"facebookLike\"\>(.*)\<\/div\>/";
    $strip_string = preg_replace($pattern, '', $string);
    return $strip_string;
  }

  /**
   * Returns the legacy article url
   *
   * @param unknown_type $old_id
   */
  protected function getOldUri($row) {
    return $row->cms_section_directory_name . '/'
      . $row->directory_name . '/' . $row->id . '/' . $this->prepareMaximBasedUrl($row->r_headline) . '.html';
  }

  /**
   * Unsets empty fields
   *
   * @param $node
   * @param $fields
   */
  public function unsetEmptyFields($node, $fields) {
    foreach ($fields as $field_name) {
      if (isset($node->{$field_name}) && (is_array($node->{$field_name}))) {
        if (isset($node->{$field_name}[LANGUAGE_NONE][0]['value']) && empty($node->{$field_name}[LANGUAGE_NONE][0]['value'])) {
          unset($node->{$field_name});
        }
      }
    }
  }

  /**
   * Append specified field to body_copy
   *
   * @param $row
   * @param $title
   * @param $field_name
   */
  public function appendTextFieldToBody($row, $title, $field_name) {
    if (!empty($row->$field_name)) {
      // Strip new lines - these get turned into <br/> tags
      $value = str_ireplace(array("\r\n", "\r", "\n"), ' ', $row->$field_name);
      $row->body_copy .= "<h3>" . $title . "</h3><p>" . $value . "</p>";
    }
  }

  public function div($a, $b) {
    return (int) ($a/$b);
  }

  public function return_dir($id) {
    $size = 500;
    $dir_name = "dir_" . $this->div($id,$size);
    return $dir_name;
  }

  /**
   * Set primary image on article nodes
   * @param unknown_type $node
   * @param unknown_type $row
   */
  protected function setMainImage($node, $row) {
    // Skip setMainImage if image is not available from source data
    if ($row->images_id == '') {
      return;
    }

    // Get image extension.
    $allowedExtensions = array('jpg', 'png', 'gif');
    $extension =in_array($row->picture_suffix, $allowedExtensions) ? $row->picture_suffix : 'jpg';
    // Construct filename.
    $filename = $this->return_dir($row->images_id) . "/it_photo_" . $row->images_id . '.' . $extension;

    // Debug output
    //echo 'Article:' . $row->id . ' - ' . $filename . ' - ' . $row->use_for_article_main_image;
    //if( $o_message ) echo $o_message;
    //echo "\n";

    $image_meta = array (
      'title' => $row->alt_tag,
      'alt_tag' => $row->alt_tag,
      'description' => $row->caption,
      'image_record_id' => $row->images_id,
      'timestamp' => strtotime($row->article_display_date),
      'image_article_headline' => $row->r_headline,
      'credits' => '',
    );

    if ($file = $this->importImage($filename, $row, $image_meta)) {
      $node->field_primary_image[LANGUAGE_NONE][0] = $file;
    }
    else {
      $message = 'Could not set primary image for node. ArticleID: @articleid .';
      watchdog('migrate', $message, array('@articleid' => $row->id));
    }
  }
  
  protected function setPdfDownload($node, $row) {
    if (!empty($row->ss_51_pdf_download)) {
      
      // Some of the links to the PDF files need cleaning up with a regex
      // I think this may be what is refered to as the DART tracking
      preg_match("/http:\/\/ad.doubleclick.net[^\?]*\?(.*)/", $row->ss_51_pdf_download, $matches);
      $uri = !empty($matches[1]) ? $matches[1] : $row->ss_51_pdf_download;
      
      $file = system_retrieve_file($uri, 'public://', TRUE, FILE_EXISTS_REPLACE);
      $furi = $file->uri;
      
      // We need to check if the file we've just grabbed is already managed
      $query = db_select('file_managed', 'f')
        ->fields('f', array('fid'))
        ->condition('f.uri', $furi, '=');
      $result = $query->execute();
      
      // If a file was already found, load it up and use it.
      if ($result->rowCount() > 0) {
        $record = $result->fetchObject();
        $existing_file = file_load($record->fid);
        $existing_file->display = 1;
        $node->field_pdf_download[LANGUAGE_NONE][0] = (array)$existing_file;
      }
      else {
        // Required property
        $file->display = 1;
        $node->field_pdf_download[LANGUAGE_NONE][0] = (array)$file;
      }
    }
 
  }

  protected function setSecondaryChannels($node, $channels) {
    global $channel_index;
    $channel_ids = explode(',',$channels);
    
    // Loop through each of the secondary channels in the comma delimited field,
    // adding each one as a taxonomy term if it doesn't already exist
    if (!empty($channel_ids)) {
      foreach ($channel_ids as $cid) {
        // Get the channel data for this this channel by loading it from the
        // global array generated from CSV data.
        $channel_data = $channel_index[$cid];
        $hierarchy = explode('>', $channel_data['New Category']);
        // The last term in the array is always the child even if it's the only term
        $term_name = trim(end($hierarchy));
        // The last but one element (if it exists) is the parent term. If it does
        // not exist then $parent_name will be empty.
        $parent_name = trim(prev($hierarchy));
        
        $tid = $this->createTaxonomyTerm($term_name, 'category', $parent_name);
        
        // Set the value of the field if tid is set
        if (!empty($tid)) {
          $node->field_category_secondary[LANGUAGE_NONE][]['tid'] = $tid;
        }
      }
    }
  }


  /**
   * Returns base video query
   */
  protected function getVideosQuery() {
    $query = Database::getConnection('default', 'autoExpressDB')
      ->select('A_movies', 'video')
      ->fields('video', array('id', 'filename', 'video_name', 'alt_html', 'video_width', 'video_height', 'video_type', 'video_size', 'r_article_id'));
    return $query;
  }

  /**
   * Fetch remote headers
   *
   * @param $url
   */
  public function getHeaders($url) {
    // Curl request to fetch headers
    $ch = curl_init();
    curl_setopt($ch, CURLOPT_URL, $url);
    curl_setopt($ch, CURLOPT_HEADER, 1);
    curl_setopt($ch, CURLOPT_NOBODY, 1);
    curl_setopt($ch, CURLOPT_RETURNTRANSFER, 1);
    $response = curl_exec ($ch);
    curl_close ($ch);

    // Parse response.
    list($split, $data) = explode("\r\n\r\n", $response, 2);
    $split = preg_split("/\r\n|\n|\r/", $split);

    list($protocol, $code, $status_message) = explode(' ', trim(array_shift($split)), 3);

    $headers = array(
      'status' => $status_message,
      'protocol' => $protocol,
    );

    // Parse headers.
    while ($line = trim(array_shift($split))) {
      list($header, $value) = explode(':', $line, 2);
      if (isset($result->headers[$header]) && $header == 'Set-Cookie') {
        // RFC 2109: the Set-Cookie response header comprises the token Set-
        // Cookie:, followed by a comma-separated list of one or more cookies.
        $headers[$header] .= ','. trim($value);
      }
      else {
        $headers[$header] = trim($value);
      }
    }

    return $headers;
  }


  /**
   * Fetches the gallery that has already been created for an article id
   *
   * @param $id
   *  The source id of the article whose gallery we want to fetch
   *
   * @return
   *  The node if of the gallery associated with this node
   *
   */
  public function getGallery($id) {
    // Load source/destination IDs from specified table
    $query = Database::getConnection('default', 'default')
      ->select('migrate_map_itprogalleriesfromchildren','mg')
      ->fields('mg', array('destid1'))
      ->condition('mg.sourceid1', $id);

    if ($result = $query->execute()->fetchAssoc()) {
      return $result['destid1'];
    }
    return FALSE;
  }

  /**
   * Get a list of embedded videos
   *
   * @param unknown_type $row
   */
  protected function getEmbeddedVideos($row) {

    // Example pattern to match [video src="AE18MayA6Avant.flv" /]
    preg_match_all('|\[video(.*?)[^/]/]|si', $row->body_copy, $video_matches);

    $embedded_videos = array();

    if (count($video_matches) > 0 && count($video_matches[1]) > 0) {
      foreach ($video_matches[1] as $i => $match) {
        if($embedded_video_details = $this->parseVideoEmbedTag($match)) {
          // Create video file objects
          $details = $this->createVideoFileObject($embedded_video_details['src']);

          // Store the matched pattern - this will be used to insert the drupal embed tag
          $details->legacy_embed_tag = $video_matches[0][$i];

          $embedded_videos[$embedded_video_details['src']] = $details;
        }
      }
    }

    return $embedded_videos;
  }

  /**
   * Extracts details from video embed tag
   */
  protected function parseVideoEmbedTag($embed_tag) {
    try {
      $video_details = array();
      $embed_element = new SimpleXMLElement('<element ' . $embed_tag . ' />');

      if ($attributes = $embed_element->attributes()) {
        foreach ($attributes as $name => $value) {
          $video_details[$name] = strval($value);
        }
      }
      return $video_details;
    }
    catch (Exception $e) {
      // Could not parse tag
      watchdog('migrate', $e->getMessage());
      return false;
    }
  }

  /**
   * Get videos list from A_movies table
   */
  protected function getAttachedVideos($row) {
    if ($row->video_count == 0) {
      // No videos attached so return empty array
      return array();
    }

    $query = $this->getVideosQuery();
    $query->condition('video.r_article_id', $row->id);
    $result = $query->execute();

    $video_details = array();

    foreach ($result as $vr) {
      $details = $this->createVideoFileObject($vr->filename);
      $details->title = $vr->video_name;
      $details->description = $vr->alt_html;
      $video_details[$vr->filename] = $details;
    }

    return $video_details;
  }

  /**
   * Generates a video file object
   *
   * - Determines the type of video being imported
   *
   * @param unknown_type $filename
   */
  protected function createVideoFileObject ($filename) {
    $details = new stdClass;

    // Check if video is YouTube
    $patterns = array(
      '@youtube\.com/watch[#\?]v=([^"\& ]+)@i',
      '@youtube\.com/embed/([^"\&\? ]+)@i',
      '@youtube\.com/v/([^"\&\? ]+)@i',
      '@youtube\.com/\?v=([^"\& ]+)@i',
      '@youtu.be/([^"\&\? ]+)@i',
    );

    foreach ($patterns as $pattern) {
      preg_match($pattern, $filename, $matches);
      if (isset($matches[1])) {
        $details->type = 'youtube';
        $details->filemime = 'video/youtube';
        $details->filename = $matches[1];
        break;
      }
    }

    // Populate details for non YouTube videos
    if (!isset($details->type) || $details->type != 'youtube') {

      $details->filename = $filename;

      // Set .mov type
      if (substr($filename, -4) == '.mov') {
        $details->type = 'mov';
        $details->filemime = 'video/x-quicktime';
        // Set .mov extension to .flv - these videos will be reencoded as .flvs
        //$details->filename = substr_replace($filename, '.flv', (strlen($filename) - 4 ), 4);
      }

      // Set .flv type
      if (substr($details->filename, -4) == '.flv') {
        $details->type = 'flv';
        $details->filemime = 'video/x-flv';
      }

      // External
      if (substr($details->filename, 0, 7) == 'http://') {
        $details->external = TRUE;
      }
    }

    return $details;
  }

  /**
   * Check if video has already been created
   *
   * @param $filename
   * @return $file
   */
  protected function checkVideoFileExists($filename) {
    $query = db_select('file_managed', 'fm')->fields('fm', array('fid'));
    $query->condition('filename', $filename);
    if ($fid = $query->execute()->fetchField()) {
      //watchdog('migrate', 'Duplicate entry: @filename', array('@filename' => $filename));
      if ($file = file_load($fid)) {
        return $file;
      }
    }
  }


  /**
   * Creates a field collection item and assigns it to parent node
   *
   * @param $parent_nid
   * @param $child_nid
   * @param $child_label
   */
  protected function assignChildToParentNode($parent_nid, $child_nid, $child_label = NULL, $group_menu_add = '1') {
    if ($node = node_load($parent_nid)) {
      $field_collection_item = entity_create('field_collection_item', array('field_name' => 'field_child_items'));
      $field_collection_item->setHostEntity('node', $node);
      $field_collection_item->field_child_page[LANGUAGE_NONE][]['nid'] = $child_nid;
      $field_collection_item->field_child_page_label[LANGUAGE_NONE][]['value'] = $this->cleanPlainText($child_label);
      $field_collection_item->field_group_menu_add[LANGUAGE_NONE][]['value'] = $group_menu_add;
      $field_collection_item->save();
    }
  }

  /**
   * Normalise sub section fields to a single alias to make mapping easier
   *
   * e.g. $row->ss_136_lead_headline becomes $row->lead_headline
   *
   * @param $row
   */
  protected function normaliseSubSectionFields ($row) {
    if (isset($this->section_fields)) {
      // Normalise each sub section field
      foreach ($this->section_fields as $field_name) {
        // Alias of joined subsection field
        $field_alias = 'ss_' . $row->subsection_select . '_' . $field_name;

        // Don't set if it already has a value
        // e.g. First join will be lead_headline - joins after follow the format ss_136_lead_headline
        if (empty($row->{$field_name}) && isset($row->{$field_alias})) {
          $row->{$field_name} = $row->{$field_alias};
        }

        // Attempt to normalise child subsection tables
        $field_alias = 'ss_' . $row->subsection_select . '_child_' . $field_name;
        if (empty($row->{$field_name}) && isset($row->{$field_alias})) {
          $row->{$field_name} = $row->{$field_alias};
        }
      }
    }
  }

  /**
   * If an optional field value is empty, remove it from the node
   *
   * - Requires optional_fields to be specified per migration class
   *
   * @param stdClass $node
   */
  public function removeEmptyOptionalFields(stdClass $node) {
    if (isset($this->optional_fields)) {
      // Optional fields must be specified per migration
      foreach ($this->optional_fields as $field_name) {
        if (isset($node->{$field_name})) {
          // Remove field from node if the value is empty
          if (empty($node->{$field_name}[LANGUAGE_NONE][0]['value'])) {
            $node->{$field_name}[LANGUAGE_NONE] = array();
          }
        }
      }
    }
  }

  /**
   * Formats tab separated string into HTML
   *
   * @param string $content
   */
  public function formatFactsAndFiguresHTML($content) {
    // Clean content so that all line breaks are the same
    $content = trim($content);
    if (preg_match("/\r/", $content) && !preg_match("/\n/", $content)) {
      $content = str_replace("\r", "\n", $content);
    }

    // Replacements
    $tab_replacement = '</td><td>';
    $newline_replacement = '</td></tr><tr><td>';
    $table_begin = ' <table><tr><th>';
    $table_end = '</td><tr></table>';

    // Replace all tabs with end-cell, begin-cell
    $content = preg_replace  ('/\t/'  , $tab_replacement  , $content);

    // Split the list on linebreaks
    $rows = preg_split  ('/\n/'  , $content);

    // Replace all linebreaks with end-row, begin-row (with or without altRow class)
    $output = '';
    foreach ($rows as $index => $row) {
      // Append newline replacement
      $row .= $newline_replacement;
      // Replace TDs with THs for first item
      $row = $index == 0 ? str_replace(array('</td><td>', '</td>'), array('</th><th>', '</th>'), $row) : $row;
      // Append to output
      $output .= $row;
    }

    // Wrap in TABLE tags
    $output = $table_begin . $output . $table_end;

    return $output;
  }

  /**
   * Creates a new taxonomy term in the specified vocabulary and returns the tid
   * - Creates new term if not available
   *
   * @param string $tag_string
   * @param string $vocab_machine_name
   */
  public function createTaxonomyTerm ($term_string, $vocab_machine_name, $parent_channel = NULL) {
    if (empty($term_string)) {
      // Don't attempt to create empty terms
      return FALSE;
    }

    if (!isset($this->taxonomy_term_cache[$vocab_machine_name])) {
      // Create empty vocabulary array if it hasn't been created
      $this->taxonomy_term_cache[$vocab_machine_name] = array();
    }
    else if (isset($this->taxonomy_term_cache[$vocab_machine_name][$term_string])) {
      // Return the cached tag
      return $this->taxonomy_term_cache[$vocab_machine_name][$term_string];
    }

    // See if this taxonomy term has a parent taxonomy associated with it.
    if (empty($parent_channel)) {
      //$parent_name
      $parent_id = 0;
    }
    else {
      // If there is a parent then recursively call this function to create it.
      $parent_id = $this->createTaxonomyTerm($parent_channel, $vocab_machine_name);
    }

    // Try to get the tid for the tag -Create if doesn't exist
    $vocabulary = taxonomy_vocabulary_machine_name_load($vocab_machine_name);
    $query = new EntityFieldQuery;
    $result = $query
      ->entityCondition('entity_type', 'taxonomy_term')
      ->propertyCondition('name', $term_string)
      ->propertyCondition('vid', (int) $vocabulary->vid)
      ->execute();
    
    $primary_tag_term = $result;
    if (empty($primary_tag_term)) {
      // Create term
      $term_data = array(
        'name' => t($term_string),
        'description' => '',
        'format' => 'filtered_html',
        'vid' => $vocabulary->vid,
        'parent' => $parent_id,
      );
      
      $term = (object) $term_data;
      taxonomy_term_save($term);
      $primary_tag_term = $term;
    }
    else {
      // Get the term object from the returned Array
      $primary_tag_term = array_pop($primary_tag_term['taxonomy_term']);
    }

    // Set term
    $tid = (int) $primary_tag_term->tid;

    // Cache the tag tid
    $this->taxonomy_term_cache[$vocab_machine_name][$term_string] = $tid;

    return $tid;
  }

  /**
   * Checks that free tag is not in the exclude list
   *
   * @param unknown_type $string
   */
  public function isAllowedFreeTag($string) {

    // Trim whitespace
    $string = trim($string);

    // Exclude tags in the array
    $exclude_tags = array(
      '', // Empty
      'n/a'
    );

    if (in_array(strtolower($string), $exclude_tags)) {
      return FALSE;
    }

    return TRUE;
  }


  /**
   * Sets the from year to article_display_date year if empty
   *
   * @deprecated We wont set a from year if one hasn't been specified by editorial
   *
   * @param unknown_type $row
   */
  public function validateFromYear($row) {
    if (empty($row->from_year)) {
      $row->from_year = date('Y', strtotime($row->article_display_date));
    }
  }


  /**
   * Clean verdict callback
   *
   * @param unknown_type $value
   */
  public function cleanVerdictText($value) {
    // Clean verdict value
    $strip_array = array(
      '<p><br mce_bogus="1"></p>',
      '<p><br></p>'
    );
    $value = str_replace($strip_array, '', $value);

    // Replace divs with new lines
    $value = str_replace('</div>', "</div>\n", $value);

    // Decode entities
    $value = html_entity_decode($value, ENT_COMPAT | ENT_QUOTES, 'UTF-8');

    return $value;
  }

  /**
   * Cleans the link_headline field of bbcode and whitespace
   *
   */
  public function cleanHeadlineText($value, $id) {
    // Check that the link_headline is not just whitespace
    $value = trim($value);
    if (empty($value)) {
      $value = NULL;
    }
    // A bit of a nasty way to check, but there are a few rows with redundant
    // bb code in the link_headline which can just be removed
    if (strpos($value, "[a href=") !== FALSE) {
      try {
        watchdog('migrate', "{$value} /// link_headline of row {$id} was set to NULL. Please check manually.");
      }
      catch (Exception $e) {
        // Do nowt - it's probably been called from a simpletestcase
      }
      $value = NULL;
    }
    return $value;
  }

  /**
   * Converts an string to a boolean (returns 1 or 0). If the string is not matched,
   * returns -1, which means error.
   */
  function stringToBoolean ($string, $true_string, $false_string) {
    if ($string === $true_string) {
      return 1;
    }
    else if ($string === $false_string) {
      return 0;
    }
    else {
      return -1;
    }

  }


  function ItProAwardsStringToBoolean($string) {
    $boolean = $this->stringToBoolean($string, "rated", "None");
    return $boolean;
  }


  function cleanAmountMoney($string) {

    $original_string = $string;

    if ($string == '') {
      return;
    }

    if (strtolower($string) == 'free') {
      return '';
    }

    if ((strpos($string, ",") !== FALSE)){
      $string = str_replace(",", "", $string);
    }

    //if the price is numeric and does not have pound symbol (£), change the format into english notation and prepend pound symbol (£) to the price
    if(is_numeric($string) && substr(trim($string), 0, 1) !== "£") {
      $string = number_format($string);
      $string = '£' . $string;
    }

    //$string = utf8_encode($string);

    if ($string != $original_string) {
      //watchdog('migrate cleanAmountMoney', $string . ' // UTF-8 encode string', NULL, WATCHDOG_INFO);
    }

    // Remove ex in the price
    $string = str_replace("ex", " ", $string);

    $string = trim($string);
    if ($string != $original_string) {
      //watchdog('migrate cleanAmountMoney', $string . ' // Removed whitespaces from beginning/end of array', NULL, WATCHDOG_INFO);
    }

    /* Commented out so that "free" can be used in the price_field
    if (strtolower($string) == 'free') {
      //watchdog('migrate cleanAmountMoney', $string . ' // Converted "Free" to 0', NULL, WATCHDOG_INFO);
      $string = 0;
    }
    */

    // Numbers are in very different formats. e.g.: "123.456.789,01", "123,456.78", "123.4", etc.
    // This tries to normalize all of them to the format "1234567.89"
    if (
         (strpos($string, ",") !== FALSE)
      || (strpos($string, ".") !== FALSE)
    ){
      $replacement = "REPLACEMENT";
      $string_copy = $string;
      //commented this out so that the price can have a comma
      //$string_copy = str_replace(",", $replacement, $string_copy);

      //commented this out so that the price can still maintain its decimal
      //$string_copy = str_replace(".", $replacement, $string_copy);

      // Convert numbers with 2 decimals
      $pattern = "/(.*)(" . $replacement . ")(\\d)(\\d)$/is";
      $string_copy = preg_replace($pattern, "$1.$2$3$4", $string_copy, -1, $count);

      // Convert numbers with 1 decimal
      $pattern = "/(.*)(" . $replacement . ")(\\d)$/is";
      $string_copy = preg_replace($pattern, "$1.$2$3$4", $string_copy, -1, $count);

      // Remove replacement token
      $string_copy = str_replace($replacement, "", $string_copy);


      //watchdog('migrate cleanAmountMoney', $string . ' // Converted to the format "123456.78 >> (' . $string_copy . ')"', NULL, WATCHDOG_INFO);
      $string = $string_copy;
    }

    /* commented this out because the price field can be a text
    if (!is_numeric($string)) {
      //watchdog('migrate cleanAmountMoney', $string . ' // Is not numeric.', NULL, WATCHDOG_WARNING);
      return -2;
    }
    */
    return $string;
  }


}

/**
 * Helper Class for Auto Express UK Migration
 */
class ITProMigrationHelper {

  private static $_instance = NULL;

  protected $recognised_agencies = array (
    // Lowercase please
    'it pro',
    'reuters',
    'pc pro',
    'additional reporting',
    'computer shopper',
    'cloud pro',
    'twitter',
    'mac user'
  );

  public function __construct() {
  }

  public static function getInstance() {
    if (NULL == self::$_instance) {
      $className = __CLASS__;
      self::$_instance = new $className;
    }
    return self::$_instance;
  }

  /**
   * Adds an email address to an existing author. It returns:
   *      -1: if the email address already exist for another author.
   *      -2: the author already has a different email address assigned. In this case the email is not updated.
   *      TRUE: in all other cases.
   */
  public function setAuthorEmail($author_nid, $email) {
    $output = TRUE;

    // Find out if this email exists already
    $query = new EntityFieldQuery();
    $result = $query->entityCondition('entity_type', 'node')
      ->propertyCondition('type', 'author')
      ->propertyCondition('nid', $author_nid, '<>')
      ->fieldCondition('field_email', 'value', $email, '=')
      ->execute();
    
    if (!empty($result)) {
      $output = -1;
    }

    // Add the email to the author
    $author_node = node_load($author_nid);
    if (!empty($author_node->field_email['und'][0]['value'])) {
      $output = -2;
    }
    else {
      $author_node->field_email['und'][0]['value'] = $email;
      node_save($author_node);
    }

    return $output;
  }


  /**
   * Returns the author nid from the passed in author name
   *
   * Author information of an article is not store in it's own tables and referenced
   * via a foreign key in the legacy Auto Express Articles tables. Instead it's stored as
   * plain text with the imported article row.
   *
   * We need to therefore populate the author content type on the Drupal site and get the
   * nid of that author. BUT FIRST!! we need to see if this author already exist.
   *
   * If the author exists return the nid of the author, if not create the author and
   * then return the nid for the reference field
   *
   * @todo Find out how does Auto Express want to treat blank author fields in imported rows.
   * @param string $author_byline
   * @return array of nids
   */
  public function getAuthorReferenceNidFromByline($author_byline) {

    if (empty($author_byline)) {
      return NULL;
    }

    $authors_names = $this->cleanByLine($author_byline);
    $ids = array();

    foreach ($authors_names as $names) {
      // @todo log any author last name longer that the fields name of 60 characters.
      // temporary fix to skip last names longer than the field names.
      // These last name have characters other that last name such as photo credits etc...
      // see if the author exists
      if (mb_strlen($names['last_name']) > 25) {
        continue;
      }
      if (mb_strlen($names['last_name']) == 0 && mb_strlen($names['first_name']) == 0) {
        continue;
      }

      $query = db_select('field_data_field_author_first_name', 'fname')
        ->fields('fname', array('entity_id', 'field_author_first_name_value'));
      $query->leftJoin('field_data_field_author_last_name', 'lname',
        "fname.entity_id = lname.entity_id");
      $query->condition('fname.field_author_first_name_value', $names['first_name'], '=')
        ->condition('lname.field_author_last_name_value', $names['last_name'], '=')
        ->range(0, 1);
      $result = $query->execute();
      if ($result->rowCount() > 0) {
        $record = $result->fetchObject();
        // Add the related data of interest
        $ids[] = $record->entity_id;
      }
      else {
        $node = new stdClass;
        $node->field_author_first_name[LANGUAGE_NONE][0]['value'] = $names['first_name'];
        $node->field_author_last_name[LANGUAGE_NONE][0]['value'] = $names['last_name'];
        // Title is usually set automatically but sometime we want to force it
        if ($names['display_name']) {
          $node->title = $names['display_name'];
        }
        $node->type = 'author';
        $node->status = 0; // All authors are unpublished by default
        $node->uid = 1;
        $node->language = LANGUAGE_NONE;
        node_save($node);
        if (isset($node->nid)) {
          $ids[] = $node->nid;
        }
      }
    }
    return $ids;

  }

  /**
   * Parses the source byline and outputs an array of the names of authors
   *
   * @param $source_byline
   *   The byline from teh source database
   *
   * @return
   *   An array containing the first and last names of the authors
   */
  public function cleanByLine($source_byline) {
    // Initialise the names array
    $names = array();

    // A lot of bylines start with "by ...". We need to get rid of this.
    $source_byline = trim($source_byline);
    if (substr($source_byline, 0, 3) == 'by ' || substr($source_byline, 0, 3) == 'By ') {
      $source_byline = substr($source_byline, 3);
    }
    // Remove html entities
    $source_byline = strip_tags($source_byline);

    // Remove single chars or single chars with a dot after them ("K" or "K.")
    $source_byline = preg_replace('/(?<!\S)[A-Za-z]{1}\.(?!\S)/i', '', $source_byline);
    $source_byline = preg_replace('/(?<!\S)[A-Za-z]{1}(?!\S)/i', '', $source_byline);
    // Some names have dots on the end of them. Replace these with spaces incase
    // they are also acting as delimiters.
    $source_byline = str_replace('.', ' ', $source_byline);
    
    // Some people have silly nick names. Remove them
    $source_byline = preg_replace('/(?<!\S)(\'.*\')(?!\S)/i', '', $source_byline);
    // Some bylines have commas on the end of them, remove these
    $source_byline = rtrim($source_byline, ",");
    // I think we can get rid of the phrase 'Additional reporting'
    $source_byline = str_replace('Additional reporting','', $source_byline);
    // Remove the word Reporter
    $source_byline = str_replace('Reporter','', $source_byline);
    $source_byline = str_replace('Correspond','', $source_byline);
    $source_byline = str_replace('Corresponent','', $source_byline);

    // There may be two names in the byline. Split them on 'and' to start with
    // $names = array();
    if (strpos($source_byline,' and ')) {
      $sub_bylines = explode(' and ', $source_byline);
    }
    else if (strpos($source_byline,' & ')) {
      $sub_bylines = explode(' & ', $source_byline);
    }
    else {
      $sub_bylines[0] = $source_byline;
    }
    $first_name = '';
    $last_name = '';
    $display_name = '';
    $parts = array();
    foreach ($sub_bylines as $sub_byline) {
      //Get rid of double spaces
      $sub_byline = preg_replace('/\s\s+/', ' ',trim($sub_byline));
      // If there are commas this is probably attributed to a news agency.
      if (strpos($sub_byline, ',')) {
        // Split again on commas
        $parts = explode(',', $sub_byline);
        // Some agency names have full stops or spaces on the end of them, remove these
        $first_name = preg_replace('/\s\s+/', ' ',rtrim(trim($parts[1]),'.'));
        $last_name = 'Content';
        $display_name = $first_name; // If the last_name is content we use only the first name as the display_name
        // This checks and logs if the news agency is not recognised and looks
        // like a name.
        $this->flagUnusualAgency($first_name, $source_byline);
      }
      else {
        $parts = explode(" ", trim($sub_byline));
        $first_name = $parts[0];
        if (!empty($parts[1])) {
          $last_name = $parts[1];
        }
        if ($last_name == 'van') { // There's an exception for "Ludwig van Winkleburg" and family.
          $last_name .= ' '.rtrim($parts[2],",");
        }
        if (empty($last_name)) {
          $last_name = 'Content';
          $display_name = $first_name;
        }
      }
      $names[] = array(
        'last_name' => substr($last_name, 0, 25),
        'first_name' => substr($first_name, 0, 25),
        'display_name' => $display_name,
      );
      
      // Double check that if the first name is in a recognised list of names,
      // the last name is always 'Content' to prevent fullnames like
      // 'Reuters with'.      
      $names = $this->normaliseAgencies($names);
    }
    return $names;
  }

  /**
   * Checks certain forms of the byline to make sure it is correctly parsed
   *
   * @param $agency
   *   A string that we expect to be a known agency.
   */
  public function flagUnusualAgency($agency, $source_byline) {
    // Do some checks on a list here.
    if (!in_array(strtolower($agency), $this->recognised_agencies)) {
      try {
        watchdog('migrate', '"'.$agency.'" /// A node looks as though it\'s byline may not be parsed properly. /// "'.$source_byline.'"');
      }
      catch (Exception $e) {
        // This is required as a work around for calling this function from a simpletest where the database is not available.
      }
    }
  }
  
  /**
   * Checks to see if the first name is one of the recognised news agencies or
   * publications and then forces the last name to be 'Content'
   *
   */
  public function normaliseAgencies($names) {
    $new_names = array();
    foreach ($names as $name) {
      // Sometimes words are misspelled
      $name['first_name'] = ($name['first_name'] == 'Reuter') ? 'Reuters' : $name['first_name'];
      $name['first_name'] = ($name['first_name'] == 'Reute') ? 'Reuters' : $name['first_name'];
      $name['first_name'] = ($name['first_name'] == 'Reuterrs') ? 'Reuters' : $name['first_name'];
      
      // The following should not have authors created at all
      if (
          ($name['first_name'] == 'at the iPhone Launch Even') ||
          ($name['first_name'] == 'Florida') ||
          ($name['first_name'] == 'London') ||
          ($name['first_name'] == 'Berlin') ||
          ($name['first_name'] == 'TEXT') ||
          ($name['first_name'] == 'CIO') ||
          ($name['first_name'] == 'in Prague') ||
          ($name['first_name'] == 'France')
          ) {
        continue;
      }
      
      if (in_array(strtolower($name['first_name']), $this->recognised_agencies)) {
        $name['first_name'] = $name['first_name'];
        $name['last_name'] = 'Content';
        $name['display_name'] = $name['first_name'];
      }
      // Follow up with some nasty exceptions
      if ($name['first_name'] == 'Reuters with additional r') {
        $name['first_name'] = 'Reuters';
        $name['last_name'] = 'Content';
        $name['display_name'] = $name['first_name'];
      }
      
      // Some news agencies don't get recognised and accidentally get broken
      // into first and last names
      if (in_array(strtolower(trim($name['first_name']).' '.trim($name['last_name'])), $this->recognised_agencies)) {
        $name['first_name'] = $name['first_name'] + ' ' + $name['last_name'];
        $name['last_name'] = 'Content';
        $name['display_name'] = $name['first_name'];
      }
      
      if ($name['first_name'] == 'Aiko' && $name['last_name'] == 'Hayashi') {
        $name['display_name'] = 'Aiko Hayashi';
      }
      
      if ($name['first_name'] == 'Ritsuko Ando') {
        $name['first_name'] = 'Ritsuko';
        $name['last_name'] = 'Ando';
        $name['display_name'] = 'Ritsuko Ando';
      }
      
      if ($name['first_name'] == 'IT') {
        $name['first_name'] = 'IT PRO';
        $name['last_name'] = 'Content';
        $name['display_name'] = 'IT PRO';
      }
      
      if ($name['display_name'] == 'Reuters') {
        $name['first_name'] = 'Reuters';
        $name['last_name'] = 'Content';
        $name['display_name'] = 'Reuters';
      }
      
      $new_names[] = $name;
    }
    return $new_names;
  }

  /**
   * Create array from a specified migration table database table
   *
   * Maps legacy ID to new node ID
   *
   * @deprecated moved to DennisMigrateToolsBase
   *
   * @param $migration_map_table
   */
  public function loadSourceMappings($migration_map_table = false) {

    $source_mappings = array();

    // Log error if no table is specified
    if (!$migration_map_table) {
      watchdog('migrate', 'loadSourceMappings() - Please specify a migration map table');
      return $source_mappings;
    }

    // Load source/destination IDs from specified table
    $query = Database::getConnection('default', 'default')
      ->select($migration_map_table,'p')
      ->fields('p', array('sourceid1', 'destid1'));

    $results = $query->execute();

    // Populate source/destination array
    foreach($results as $row) {
      $source_mappings[$row->sourceid1] = $row->destid1;
    }

    return $source_mappings;
  }

  /**
   * Load all the children from the source for a particular parent nid
   *
   * @param $nid
   *  The entity id of the parent
   **/
  public function getChildrenFromSource ($nid = NULL) {
    if (!isset($nid)) {
      return FALSE;
    }

    $query = Database::getConnection('default', 'itpro')
      ->select('A_articles', 'a')
      ->fields('a', array('id'))
      ->condition('a.parent_article', $nid);

    return $query->execute()->fetchCol();
  }
  
  /**
   * Fetches a CSV file, creates a PHP array and writes the code to a new file
   *
   * To be used for a one-off conversion of CSV to PHP array, which can then
   * be used as a lookup table.
   *
   * Run this code from /devel/php using something like this:
   *
   * $helper = new ITProMigrationHelper;
   * $helper->csvToArray('/home/edward/Downloads/IT PRO Category mapping.csv');
   *
   * Then move the created file from public:// into the magazine_migrate folder
   *
   * @param $fin
   *  Filepath of the csv file
   * @param $fout
   *  Filepath of the destination file to be created
   */
  public function csvToArray($fin, $fout = 'public://magazine_migrate_channels.inc') {
    $file = array();
    $row = array();
    if (($handle = fopen($fin, "r")) !== FALSE) {
      // Get the first line as the headers
      $headers = $row = fgetcsv($handle, 0, ",");
      while (($row = fgetcsv($handle, 0, ",")) !== FALSE) {
        $filedata[$row[0]] = array_combine($headers, $row);
      }
      fclose($handle);
    }
    
    if (($handle = fopen($fout, "w")) !== FALSE) {
      $content = "<?php\n\n";
      $content .= "global \$channel_index;\n";
      $content .= "\$channel_index = ";
      $content .= var_export($filedata, TRUE);
      $content .= ";";
      fwrite($handle, $content);
      fclose($handle);
    }
  }
}

/**
 * Migrate IT Pro News Articles
 *
 */
class ITProNewsArticlesMigration extends ITProArticleBaseMigration {

  public function __construct() {

    $this->description = t('Migrate IT Pro news articles');

    $query = $this->getArticlesQuery();

    $query->leftjoin('S_subsection_39', 'ss_39', 'ss_39' . '.r_article_id = a.id');
    foreach ($this->section_fields as $field) {
      $query->addField('ss_39', $field, 'ss_39'.'_'.$field);
    }

    // @todo Get the list of other fields that need migrating for this subsection.
    $query->condition('a.subsection_select', $this->subsection_ids['News']);

    $this->source = new MigrateSourceSQL($query, array(), NULL,
      array('map_joinable' => FALSE, 'cache_counts' => FALSE));
    $this->destination = new MigrateDestinationNode('article', array('text_format' => 'filtered_html'));

    // Gallery source migration mapping.
    $this->addFieldMapping('field_gallery', 'id')
      ->sourceMigration('ITProStandardGalleries');

    parent::__construct();
  }

  /**
   * Set the node field_article_type
   *
   * Sets the field_article_type after the parent class prepare() method has done it's job.
   */
  public function prepare(stdClass $node, stdClass $row) {
    parent::prepare($node, $row);
  }

  /**
   * Checks to see if this row should be processed and hands back
   * to the parent prepareRow($row) if needed. This is called before
   * any mapping is done.
   *
   * @param unknown_type $row
   */
  public function prepareRow($row) {
    /*
    // Enter any conditions to exclude rows from import

    // Normalise sub section fields
    // - should be called before any functions working on sub section fields
    $this->normaliseSubSectionFields($row);

    // Append extra info to body
    $title = !empty($row->boxout_html_header) ? $row->boxout_html_header : 'Extra Info';
    $this->appendTextFieldToBody($row, $title, 'boxout_html');*/

    return parent::prepareRow($row);
  }
}


class ITProHowtoArticlesMigration extends ITProArticleBaseMigration {

  public function __construct() {

    $this->description = t('Migrate IT Pro How tos');

    $query = $this->getArticlesQuery();
    $query->leftjoin('S_subsection_42', 'ss_42', 'ss_42' . '.r_article_id = a.id');

    foreach ($this->section_fields as $field) {
      $query->addField('ss_42', $field, 'ss_42'.'_'.$field);
    }

    // @todo Get the list of other fields that need migrating for this subsection.
    $query->condition('a.subsection_select', $this->subsection_ids['How tos']);
    // Child and non-child Howtos will be migrated separately. Here we'll just handle the non-children
    $query->condition('a.parent_article', '0');

    $this->source = new MigrateSourceSQL($query, array(), NULL,
      array('map_joinable' => FALSE, 'cache_counts' => FALSE));
    $this->destination = new MigrateDestinationNode('article', array('text_format' => 'filtered_html'));

    $this->addFieldMapping('field_gallery', 'id')
      ->description(t('Mapping to field_gallery on the destination. This will be handled in prepareRow()'));

    parent::__construct();
  }

  /**
   * Set the node field_article_type
   *
   * Sets the field_article_type after the parent class prepare() method has done it's job.
   */
  public function prepare(stdClass $node, stdClass $row) {
    // Associate the gallery previously migrated for this How to using a node
    // ref
    if (!empty($row->gallery_id)) {
      $node->field_gallery[LANGUAGE_NONE][0]['nid'] = $row->gallery_id;
    }
    parent::prepare($node, $row);
  }

  /**
   * Checks to see if this row should be processed and hands back
   * to the parent prepareRow($row) if needed. This is called before
   * any mapping is done.
   *
   * @param unknown_type $row
   */
  public function prepareRow($row) {
    // We need to get the correct gallery id for this How to. It should already
    // have been migrated, so just look it up in the correct table
    $row->gallery_id = $this->getGallery($row->id);
    if (empty($row->gallery_id)) {
      watchdog('migrate', $row->id.' /// There was no migrated gallery associated with this source How to');
    }
    return parent::prepareRow($row);
  }
}


class ITProReviewArticlesMigration extends ITProArticleBaseMigration {

  public function __construct() {

    $this->description = t('Migrate IT Pro Reviews');

    $query = $this->getArticlesQuery();

    $query->leftjoin('S_subsection_45', 'ss_45', 'ss_45.r_article_id = a.id');
    $query->leftjoin('CMS_product', 'cp', 'cp.id = ss_45.product_pick');

    foreach ($this->section_fields as $field) {
      $query->addField('ss_45', $field, 'ss_45'.'_'.$field);
    }

    // @todo Get the list of other fields that need migrating for this subsection.
    $query->condition('a.subsection_select', $this->subsection_ids['Reviews']);

    $this->source = new MigrateSourceSQL($query, array(), NULL,
      array('map_joinable' => FALSE, 'cache_counts' => FALSE));
    $this->destination = new MigrateDestinationNode('review', array('text_format' => 'filtered_html'));

    // Get the fields we need from the subsection table specific to this article
    // type
    $query->addField('ss_45', 'pros', 'ss_45_pros');
    $query->addField('ss_45', 'cons', 'ss_45_cons');
    $query->addField('ss_45', 'supplier', 'ss_45_supplier');
    $query->addField('ss_45', 'rating', 'ss_45_rating');
    $query->addField('ss_45', 'verdict', 'ss_45_verdict');
    $query->addField('ss_45', 'it_pro_awards', 'ss_45_it_pro_awards');
    $query->addField('ss_45', 'price_info', 'ss_45_price_info');
    $query->addField('cp', 'price_info', 'cp_price_info');
    $query->addField('ss_45', 'price', 'ss_45_price');
    $query->addField('cp', 'price', 'cp_price');
    $query->addField('ss_45', 'system_specs', 'ss_45_specs');

    parent::__construct();

    // Pros field
    $this->addFieldMapping('field_pros', 'ss_45_pros')
      ->callbacks(array($this, 'cleanHtml'), array($this, 'cleanPlainText'))
      ->description(t('Mapping of field_pros'));
    // Cons field
    $this->addFieldMapping('field_cons', 'ss_45_cons')
      ->callbacks(array($this, 'cleanHtml'), array($this, 'cleanPlainText'))
      ->description(t('Mapping of field_cons'));
    // Supplier field
    $this->addFieldMapping('field_supplier', 'ss_45_supplier')
      ->callbacks(array($this, 'cleanHtml'), array($this, 'cleanPlainText'))
      ->description(t('Mapping of field_supplier'));
    // 5 star Rating field. This is processed in the prepare function
    $this->addFieldMapping('field_review_score_overall')
      ->description(t('Mapping of field_review_score_overall'));
    // Verdict field
    $this->addFieldMapping('field_verdict', 'ss_45_verdict')
      ->callbacks(array($this, 'cleanHtml'), array($this, 'cleanPlainText'))
      ->description(t('Mapping of field_verdict'));
    // It Pro Awards field
    $this->addFieldMapping('field_award_logo', 'ss_45_it_pro_awards')
      ->callbacks(array($this, 'ItProAwardsStringToBoolean'))
      ->description(t('Mapping of field_award_logo'));
    // Price info field
    $this->addFieldMapping('field_price_info', 'ss_45_price_info')
      ->callbacks(array($this, 'cleanHtml'), array($this, 'cleanPlainText'))
      ->description(t('Mapping of field_price_info'));
    // Price field
    $this->addFieldMapping('field_price', 'ss_45_price')
      ->callbacks(array($this, 'cleanHtml'), array($this, 'cleanAmountMoney'))
      ->description(t('Mapping of field_price'));
    // Specifications field
    $this->addFieldMapping('field_specifications', 'ss_45_specs')
      ->callbacks(array($this, 'cleanHtml'), array($this, 'cleanPlainText'))
      ->description(t('Mapping of field_specifications'));
    // Gallery source migration mapping.
    $this->addFieldMapping('field_gallery', 'id')
      ->sourceMigration('ITProStandardGalleries');
  }

  /**
   * Set the node field_article_type
   *
   * Sets the field_article_type after the parent class prepare() method has done it's job.
   */
  public function prepare(stdClass $node, stdClass $row) {
    // We'll set the value of the rating field on the node
    $node->field_review_score_overall[LANGUAGE_NONE][0]['rating'] = $this->fiveStarPercentage($row->ss_45_rating);
    parent::prepare($node, $row);
  }

  /**
   * Checks to see if this row should be processed and hands back
   * to the parent prepareRow($row) if needed. This is called before
   * any mapping is done.
   *
   * @param unknown_type $row
   */
  public function prepareRow($row) {
    // If the price from the subsection table is empty, use the price from the CMS_product table
    // Uses product pick from the subsection table that is joined with id from the CMS_product table
    if($row ->ss_45_price == '') {
      $row ->ss_45_price = $row ->cp_price;
    }

    //If the price_info from the subsection table is empty, use the price_info from the CMS_product table
    //Uses product pick from the subsection table that is joined with id from the CMS_product table
    if($row ->ss_45_price_info == '') {
      $row ->ss_45_price_info = $row ->cp_price_info;
    }
    return parent::prepareRow($row);
  }
}

class ITProLabArticlesMigration extends ITProArticleBaseMigration {

  public function __construct() {

    $this->description = t('Migrate IT Pro Labs');

    $query = $this->getArticlesQuery();
    $query->leftjoin('S_subsection_48', 'ss_48', 'ss_48' . '.r_article_id = a.id');

    foreach ($this->section_fields as $field) {
      $query->addField('ss_48', $field, 'ss_48'.'_'.$field);
    }

    // @todo Get the list of other fields that need migrating for this subsection.
    $query->condition('a.subsection_select', $this->subsection_ids['Labs']);
    // Child and non-child Labs will be migrated separately. Here we'll just handle the parents
    $query->condition('a.parent_article', '0');

    $this->source = new MigrateSourceSQL($query, array(), NULL,
      array('map_joinable' => FALSE, 'cache_counts' => FALSE));
    $this->destination = new MigrateDestinationNode('group_content', array('text_format' => 'filtered_html'));

    // Get the fields we need from the subsection table specific to this article
    // type
    $query->addField('ss_48', 'verdict', 'ss_48_verdict');
    $query->addField('ss_48', 'it_pro_awards', 'ss_48_it_pro_awards');
    $query->addField('ss_48', 'lab_conclusion', 'ss_48_lab_conclusion');
    $query->addField('ss_48', 'how_we_tested', 'ss_48_how_we_tested');
    // $query->addField('ss_48', 'system_specs', 'ss_48_specs');

    parent::__construct();

    // Verdict field
    $this->addFieldMapping('field_verdict', 'ss_48_verdict')
      ->callbacks(array($this, 'cleanHtml'), array($this, 'cleanPlainText'))
      ->description(t('Mapping of field_verdict'));
    // It Pro Awards field
    $this->addFieldMapping('field_award_logo', 'ss_48_it_pro_awards')
      ->callbacks(array($this, 'ItProAwardsStringToBoolean'))
      ->description(t('Mapping of field_award_logo'));
    // Conclusion field
    $this->addFieldMapping('field_lab_conclusion', 'ss_48_lab_conclusion')
      ->callbacks(array($this, 'cleanHtml'), array($this, 'cleanPlainText'))
      ->description(t('Mapping of field_lab_conclusion'));
    // Gallery source migration mapping.
    $this->addFieldMapping('field_gallery', 'id')
      ->sourceMigration('ITProStandardGalleries');
    $this->dependencies[] = 'ITProLabChildren';
  }

  /**
   * Set the node field_article_type
   *
   * Sets the field_article_type after the parent class prepare() method has done it's job.
   */
  public function prepare(stdClass $node, stdClass $row) {
    parent::prepare($node, $row);
  }

  /**
   * Checks to see if this row should be processed and hands back
   * to the parent prepareRow($row) if needed. This is called before
   * any mapping is done.
   *
   * @param unknown_type $row
   */
  public function prepareRow($row) {
    return parent::prepareRow($row);
  }

  public function complete(stdClass $entity, stdClass $row) {

    if (!empty($row->ss_48_how_we_tested)) {
      watchdog("migrate", 'The field how_we_tested needs to be migrated manually (node nid: ' . $entity->nid . '. source row id: ' . $row->id . ')');
    }

    // We're going to handle the parent child relationships in the reverse order
    // to the autoexpress example
    $helper = ITProMigrationHelper::getInstance();

    $children_ids = $helper->getChildrenFromSource($row->id);
    // After querying the source table for the child nodes of this parent, we
    // need to get the node ids for each one
    $children_mappings = $helper->loadSourceMappings('migrate_map_itprolabchildren');

    $child_items = array();
    foreach ($children_ids as $id) {
      if (isset($children_mappings[$id])) {
        $this->assignChildToParentNode($entity->nid, $children_mappings[$id]);
      }
    }
    
    // No idea why, but if this is run at the top of the complete method, the
    // checkbox on the URL alias settings does not get unchecked and the URL
    // alias test fails.
    parent::complete($entity, $row);
  }
}

class ITProLabChildrenMigration extends ITProArticleBaseMigration {

  public function __construct() {

    $this->description = t('Migrate IT Pro Labs Children');

    $query = $this->getArticlesQuery();
    $query->leftjoin('S_subsection_48', 'ss_48', 'ss_48' . '.r_article_id = a.id');

    foreach ($this->section_fields as $field) {
      $query->addField('ss_48', $field, 'ss_48'.'_'.$field);
    }

    // @todo Get the list of other fields that need migrating for this subsection.
    $query->condition('a.subsection_select', $this->subsection_ids['Labs']);
    // Child and non-child Labs will be migrated separately. Here we'll just handle the children
    $query->condition('a.parent_article', '0', '>');

    // Add on the subsection table for the child and get the fields we need from it
    $query->leftjoin('S_subsection_48_child', 'ss_48_child', 'ss_48_child.r_article_id = a.id');
    $query->addField('ss_48_child', 'pros', 'ss_48_pros');
    $query->addField('ss_48_child', 'cons', 'ss_48_cons');
    $query->addField('ss_48_child', 'supplier', 'ss_48_supplier');
    $query->addField('ss_48_child', 'rating', 'ss_48_rating');
    $query->addField('ss_48_child', 'verdict', 'ss_48_verdict');
    $query->addField('ss_48_child', 'it_pro_awards', 'ss_48_it_pro_awards');
    $query->addField('ss_48_child', 'price_info', 'ss_48_price_info');
    $query->addField('ss_48_child', 'price', 'ss_48_price');
    $query->addField('ss_48_child', 'system_specs', 'ss_48_specs');

    $this->source = new MigrateSourceSQL($query, array(), NULL,
      array('map_joinable' => FALSE, 'cache_counts' => FALSE));
    $this->destination = new MigrateDestinationNode('review', array('text_format' => 'filtered_html'));

    // Pros field
    $this->addFieldMapping('field_pros', 'ss_48_pros')
      ->callbacks(array($this, 'cleanHtml'), array($this, 'cleanPlainText'))
      ->description(t('Mapping of field_pros'));
    // Cons field
    $this->addFieldMapping('field_cons', 'ss_48_cons')
      ->callbacks(array($this, 'cleanHtml'), array($this, 'cleanPlainText'))
      ->description(t('Mapping of field_cons'));
    // Supplier field
    $this->addFieldMapping('field_supplier', 'ss_48_supplier')
      ->callbacks(array($this, 'cleanHtml'), array($this, 'cleanPlainText'))
      ->description(t('Mapping of field_supplier'));
    // 5 star rating field. This will be processed in the prepare function
    $this->addFieldMapping('field_review_score_overall')
      ->description(t('Mapping of field_review_score_overall'));
    // Verdict field
    $this->addFieldMapping('field_verdict', 'ss_48_verdict')
      ->callbacks(array($this, 'cleanHtml'), array($this, 'cleanPlainText'))
      ->description(t('Mapping of field_verdict'));
    // It Pro Awards field
    $this->addFieldMapping('field_award_logo', 'ss_48_it_pro_awards')
      ->callbacks(array($this, 'ItProAwardsStringToBoolean'))
      ->description(t('Mapping of field_award_logo'));
    // Price info field
    $this->addFieldMapping('field_price_info', 'ss_48_price_info')
      ->callbacks(array($this, 'cleanHtml'), array($this, 'cleanPlainText'))
      ->description(t('Mapping of field_price_info'));
    // Price field
    $this->addFieldMapping('field_price', 'ss_48_price')
      ->callbacks(array($this, 'cleanHtml'), array($this, 'cleanAmountMoney'))
      ->description(t('Mapping of field_price'));
    // Specifications field
    $this->addFieldMapping('field_specifications', 'ss_48_specs')
      ->callbacks(array($this, 'cleanHtml'), array($this, 'cleanPlainText'))
      ->description(t('Mapping of field_specifications'));
    // Gallery source migration mapping.
    $this->addFieldMapping('field_gallery', 'id')
      ->sourceMigration('ITProStandardGalleries');
    parent::__construct();
  }

  /**
   * Set the node field_article_type
   *
   * Sets the field_article_type after the parent class prepare() method has done it's job.
   */
  public function prepare(stdClass $node, stdClass $row) {
    // We'll set the value of the rating field on the node
    $node->field_review_score_overall[LANGUAGE_NONE][0]['rating'] = $this->fiveStarPercentage($row->ss_48_rating);
    parent::prepare($node, $row);
  }

  /**
   * Checks to see if this row should be processed and hands back
   * to the parent prepareRow($row) if needed. This is called before
   * any mapping is done.
   *
   * @param unknown_type $row
   */
  public function prepareRow($row) {
    return parent::prepareRow($row);
  }
}




class ITProFeaturesArticlesMigration extends ITProArticleBaseMigration {

  public function __construct() {
    $this->description = t('Migrate IT Pro Features');

    $query = $this->getArticlesQuery();
    $query->leftjoin('S_subsection_51', 'ss_51', 'ss_51' . '.r_article_id = a.id');

    foreach ($this->section_fields as $field) {
      $query->addField('ss_51', $field, 'ss_51'.'_'.$field);
    }

    // Features have a boxout_text column in the subsection table which does not
    // need migrating (there are only 3 of them) but we need to log if they're
    // there. We also need to log pdf_downloads
    $query->addField('ss_51', 'boxout_text', 'ss_51_boxout_text');
    $query->addField('ss_51', 'PDF_download', 'ss_51_pdf_download');
    $query->condition('a.subsection_select', $this->subsection_ids['Features']);

    $this->source = new MigrateSourceSQL($query, array(), NULL,
      array('map_joinable' => FALSE, 'cache_counts' => FALSE));
    $this->destination = new MigrateDestinationNode('article', array('text_format' => 'filtered_html'));

    // Gallery source migration mapping.
    $this->addFieldMapping('field_gallery', 'id')
      ->sourceMigration('ITProStandardGalleries');

    parent::__construct();
  }

  /**
   * Set the node field_article_type
   *
   * Sets the field_article_type after the parent class prepare() method has done it's job.
   */
  public function prepare(stdClass $node, stdClass $row) {
    parent::prepare($node, $row);

    // We need to flag an issue if there is boxout_text available
    // Truncate it intelligently for the log message
    $boxout = truncate_utf8(strip_tags(trim($row->ss_51_boxout_text)), 15, TRUE, TRUE);
    if (!empty($boxout)) {
      // When run from prepareRow instead of prepare, on the first call to
      // watchdog the message is outputted to the screen instead of the log.
      // All subsequent calls seem to work OK. In the class
      // ITProHowtoChildrenMigration there is a wachdog call in prepareRow that
      // works fine every time.
      watchdog('migrate', '"%boxout" /// boxout_text of row %id was not migrated. Please check manually.', array('%boxout' => $boxout, '%id' => $row->id), WATCHDOG_NOTICE);
    }
    
    // Migrate any pdf files present. An example of a source row that has one is
    // 629482
    $this->setPdfDownload($node, $row);
  }

  public function complete(stdClass $entity, stdClass $row) {
    
  }

  /**
   * Checks to see if this row should be processed and hands back
   * to the parent prepareRow($row) if needed. This is called before
   * any mapping is done.
   *
   * @param unknown_type $row
   */
  public function prepareRow($row) {
    return parent::prepareRow($row);
  }
}


class ITProResearchArticlesMigration extends ITProArticleBaseMigration {

  public function __construct() {
    $this->description = t('Migrate IT Pro Research');

    $query = $this->getArticlesQuery();
    $query->leftjoin('S_subsection_71', 'ss_71', 'ss_71' . '.r_article_id = a.id');
    $query->addField('ss_71', 'PDF_Download', 'ss_71'.'_'.'pdf_download');
    // @todo Get the list of other fields that need migrating for this subsection.
    $query->condition('a.subsection_select', $this->subsection_ids['Research']);

    $this->source = new MigrateSourceSQL($query, array(), NULL,
      array('map_joinable' => FALSE, 'cache_counts' => FALSE));
    $this->destination = new MigrateDestinationNode('article', array('text_format' => 'filtered_html'));

    // Gallery source migration mapping.
    $this->addFieldMapping('field_gallery', 'id')
      ->sourceMigration('ITProStandardGalleries');

    parent::__construct();
  }

  /**
   * Set the node field_article_type
   *
   * Sets the field_article_type after the parent class prepare() method has done it's job.
   */
  public function prepare(stdClass $node, stdClass $row) {
    parent::prepare($node, $row);
  }

  /**
   * Checks to see if this row should be processed and hands back
   * to the parent prepareRow($row) if needed. This is called before
   * any mapping is done.
   *
   * @param unknown_type $row
   */
  public function prepareRow($row) {
    return parent::prepareRow($row);
  }

  public function complete(stdClass $entity, stdClass $row) {
    if (!empty($row->ss_71_pdf_download)) {
      watchdog("migrate", 'The field pdf_download needs to be migrated manually (node nid: ' . $entity->nid . '. source row id: ' . $row->id . ')');
    }
  }
}

class ITProBlogArticlesMigration extends ITProArticleBaseMigration {

  public function __construct() {

    $this->description = t('Migrate IT Pro blog posts');

    $query = $this->getArticlesQuery();

    // @todo Get the list of other fields that need migrating for this subsection.
    $query->condition('a.subsection_select', $this->subsection_ids['Blog']);

    $this->source = new MigrateSourceSQL($query, array(), NULL,
      array('map_joinable' => FALSE, 'cache_counts' => FALSE));
    $this->destination = new MigrateDestinationNode('article', array('text_format' => 'filtered_html'));

    // Gallery source migration mapping.
    $this->addFieldMapping('field_gallery', 'id')
      ->sourceMigration('ITProStandardGalleries');

    parent::__construct();
  }

  /**
   * Set the node field_article_type
   *
   * Sets the field_article_type after the parent class prepare() method has done it's job.
   */
  public function prepare(stdClass $node, stdClass $row) {
    parent::prepare($node, $row);
  }

  /**
   * Checks to see if this row should be processed and hands back
   * to the parent prepareRow($row) if needed. This is called before
   * any mapping is done.
   *
   * @param unknown_type $row
   */
  public function prepareRow($row) {
    return parent::prepareRow($row);
  }
}
